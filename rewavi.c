#include "common.h"
#include <mmreg.h>

#define SUBFORMAT_GUID(fmt) \
    {fmt, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}}

#define PRINT_LOG(level, ...) \
    rewavi_log("rewavi", level, __VA_ARGS__)

#define CLOSE_IF_ERR(cond, ...) \
    if (cond) {retcode = PRINT_LOG(LOG_ERROR, __VA_ARGS__); goto close;}

#define DEFAULT_MASK_COUNT 9
#define DEFAULT_MASK {0, 4, 3, 259, 51, 55, 63, 319, 255}
#define BUFFSIZE 0x20000 /* 128KiB */

static void usage(int ret);
static void usage2(void);
static uint32_t numofbits(uint32_t bits);

int main(int argc, char **argv)
{
    if (argc < 2)
        usage(1);

    if (!strcmp(argv[1], "-h"))
        usage2();

    int retcode = 0;

    if (!argv[2] || !stricmp(argv[2], "-x") || !stricmp(argv[2], "-r")) {
        PRINT_LOG(LOG_WARNING, "output target is not specified.\n");
        retcode = 1;
    }

    AVIFileInit();
    FILE* output_fh = NULL;
    PAVIFILE avifile = NULL;
    PAVISTREAM avistream = NULL;
    AVISTREAMINFO stream_info;
    WAVEFORMATEX wavefmt;
    LONG fmtsize = sizeof(WAVEFORMATEX);
    int id = 0;

    CLOSE_IF_ERR(AVIFileOpen(&avifile, argv[1], OF_READ | OF_SHARE_DENY_WRITE, NULL),
                "Could not open AVI file \"%s\".\n", argv[1]);

    while (1) {
        if (AVIFileGetStream(avifile, &avistream, 0, id))
            break;
        if (AVIStreamInfo(avistream, &stream_info, sizeof(AVISTREAMINFO)))
            goto release_stream;
        if (stream_info.fccType != streamtypeAUDIO)
            goto release_stream;
        PRINT_LOG(LOG_INFO, "Found an audio track(ID %d) in %s\n", id, argv[1]);
        if (AVIStreamReadFormat(avistream, 0, &wavefmt, &fmtsize))
            goto release_stream;
        if (wavefmt.wFormatTag == WAVE_FORMAT_PCM ||
            wavefmt.wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
            break;
release_stream:
        AVIStreamRelease(avistream);
        avistream = NULL;
        id++;
    }

    CLOSE_IF_ERR(!avistream, "Could not find PCM audio track in \"%s\".\n", argv[1]);

    PRINT_LOG(LOG_INFO, "streamID %d: %uchannels, %uHz, %ubits, %.3fseconds.\n",
               id, wavefmt.nChannels, wavefmt.nSamplesPerSec, wavefmt.wBitsPerSample,
               1.0 * stream_info.dwLength * stream_info.dwScale / stream_info.dwRate);

    if (wavefmt.wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
        PRINT_LOG(LOG_INFO, "Audio track contains floating-point samples.\n");

    CLOSE_IF_ERR(BUFFSIZE < wavefmt.nBlockAlign,
                 "Too large samples; audio track is definitely invalid.\n");

    if (retcode)
        goto close;

    uint32_t chmask = 0;
    if (!stricmp(argv[3], "-x")) {
        if (!argv[4]) {
            uint32_t default_chmask[DEFAULT_MASK_COUNT] = DEFAULT_MASK;
            chmask = wavefmt.nChannels < DEFAULT_MASK_COUNT ?
                     default_chmask[wavefmt.nChannels] : (1 << wavefmt.nChannels) - 1;
        } else {
            chmask = atoi(argv[4]);
            CLOSE_IF_ERR(numofbits(chmask) != wavefmt.nChannels,
                         "Invalid channel mask was specified.\n");
        }
    }

    int dupout = 0;
    if (!strcmp(argv[2], "-")) {
        dupout = _dup(_fileno(stdout));
        fclose(stdout);
        _setmode(dupout, _O_BINARY);
        output_fh = _fdopen(dupout, "wb");
    } else
        output_fh = fopen(argv[2], "wb");

    CLOSE_IF_ERR(!output_fh, "Fail to create/open file.\n");

    char buffer[BUFFSIZE];
    uint32_t samples_in_buffer = BUFFSIZE / wavefmt.nBlockAlign;
    LONG samples_read;
    LONG nextsample = 0;

    if (!stricmp(argv[3], "-r"))
        goto write_data;

    uint32_t headersize = chmask ? 60 : 36;
    uint64_t filesize = (uint64_t)wavefmt.nBlockAlign * stream_info.dwLength;
    if (filesize > 0xFFFFFFFF - headersize) {
        PRINT_LOG(LOG_WARNING, "WAV file will be larger than 4GB.\n");
        filesize = wavefmt.nBlockAlign * (0xFFFFFFFF - headersize / wavefmt.nBlockAlign);
    }
    uint32_t size_for_write = (uint32_t)filesize + headersize;

    /* riff file header */
    fwrite("RIFF", 1, 4, output_fh);
    fwrite(&size_for_write, 4, 1, output_fh);
    fwrite("WAVE", 1, 4, output_fh);

    /* format chunk */
    fwrite("fmt ", 1, 4, output_fh);
    size_for_write = chmask ? 40 : 16;
    fwrite(&size_for_write, 4, 1, output_fh);
    /* As for the WAVEFORMATEX / WAVEFORMATEXTENSIBLE structure,
        alignment is taken into consideration. */
    if (!chmask)
        fwrite(&wavefmt, fmtsize - 2, 1, output_fh); /* never write cbSize! */
    else {
        WAVEFORMATEXTENSIBLE wavefmt_ext = {
            wavefmt, {wavefmt.wBitsPerSample}, chmask,
            SUBFORMAT_GUID(wavefmt.wFormatTag)};
        wavefmt_ext.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        wavefmt_ext.Format.cbSize = 22;
        fwrite(&wavefmt_ext, sizeof(WAVEFORMATEXTENSIBLE), 1, output_fh);
    }

    /* data chunk*/
    fwrite("data", 1, 4, output_fh);
    size_for_write = (uint32_t)filesize;
    fwrite(&size_for_write, 4, 1, output_fh);

    fflush(output_fh);

write_data:
    PRINT_LOG(LOG_INFO, "Writing WAV/RAW file %s ...\n", dupout ? "to stdout" : argv[2]);
    /* fraction processing at first. */
    AVIStreamRead(avistream, 0, stream_info.dwLength % samples_in_buffer,
                  &buffer, BUFFSIZE, NULL, &samples_read);
    while (nextsample < stream_info.dwLength) {
        fwrite(buffer, wavefmt.nBlockAlign, samples_read, output_fh);
        nextsample += samples_read;
        AVIStreamRead(avistream, nextsample, samples_in_buffer, &buffer,
                      BUFFSIZE, NULL, &samples_read); /* The last reading is just ignored. */
    }

    fflush(output_fh);

    CLOSE_IF_ERR(nextsample != stream_info.dwLength, "Writing failed.\n");
    PRINT_LOG(LOG_INFO, "File written successfully.\n");

close:
    if (output_fh)
        fclose(output_fh);
    if (avistream)
        AVIStreamRelease(avistream);
    if (avifile)
        AVIFileRelease(avifile);
    AVIFileExit();
    return retcode;
}

static void usage(int ret)
{
    fprintf(stderr,
        "REWAVI - rewritten/modified WAVI ver.%s\n"
        " (c) 2011 Oka Motofumi\n"
        " WAVI original - (c) 2007 Tamas Kurucsai, with lots of help from tebasuna51\n"
        "Licensed under the terms of the GNU General Public License version2 or later.\n"
        "\n"
        "Usage: rewavi <input> <output> [ -r | -x [<mask>] ]\n"
        "  input  : input AVI1.0( or AviSynth Script)file name.\n"
        "  output : output WAV file name. '-' means output to stdout\n"
        "  -r : Write a raw file of samples without the WAV header.\n"
        "  -x : Write an extended WAV header containing channel mask for\n"
        "       multi-channel audio.\n"
        "       If channel mask is not specified, the defaut value will be set.\n"
        "\n"
        " 'rewavi -h' will display more complicated description.\n", VERSION);
    if (ret)
        exit(ret);
}

static void usage2(void)
{
    usage(0);
    fprintf(stderr,
        "\n"
        "This utility extracts the first uncompressed PCM audio track\n"
        "from an AVI1.0 file and saves it to a WAV file. This is not quite\n"
        "useful for most AVI files since they usually contain some kind\n"
        "of compressed audio, but it can come handy when it's needed to\n"
        "save an audio track from AviSynth.\n"
        "\n"
        "If <input> is a valid AVI1.0 file which contains PCM audio,\n"
        "the audio track will be written to <output> as a WAV(or RAW) file.\n"
        "If '-' is passed as <output>, the WAV file will be written\n"
        "to the standard output.\n"
        "If <output> is not given, only information will be printed\n"
        "about the audio track.\n"
        "\n"
        "On success, the exit code will be 0 and the first line printed\n"
        "to the standard error will look like the following:\n"
        "    streamID <n>: <c>channels, <r>Hz, <b>bits, <l>sec.\n"
        "\n"
        "Where <n> is IDnumber of audio track that comtaining PCM audio, <c> is\n"
        "the number of audio channels, <r> is the sampling rate, <b> is the number\n"
        "of bits per sample and <l> is the length of the track in seconds.\n"
        "\n"
        "If the audio track contains floating-point samples, the next line\n"
        "printed to the standard error will be:\n"
        "Audio track contains floating-point samples.\n"
        "\n"
        "If an error occurs, the exit code will be 1 and some useful error\n"
        "message will be printed to the standard error.\n"
        "\n"
        "WAV files larger than 4 GB may be created. However, such WAV files\n"
        "are non-standard and may not be handled correctly by some players\n"
        "and encoders. A warning will be printed to the standard error when\n"
        "such a WAV file is created.\n"
        "\n"
        "The default channel masks are:\n"
        "\n"
        "Mask  Ch.  MS channels                   Description\n"
        "----  ---  ----------------------------  ----------------\n"
        "   4   1                       FC        Mono\n"
        "   3   2                          FR FL  Stereo\n"
        " 259   3   BC                     FR FL  First Surround\n"
        "  51   4              BR BL       FR FL  Quadro\n"
        "  55   5              BR BL    FC FR FL  like Dpl II (without LFE)\n"
        "  63   6              BR BL LF FC FR FL  Standard Surround\n"
        " 319   7   BC         BR BL LF FC FR FL  With back center\n"
        " 255   8      FLC FRC BR BL LF FC FR FL  With front center left/right\n"
        "\n"
        "Some other common channel masks:\n"
        "\n"
        "Mask  Ch. MS channels                    Description\n"
        "----  --- -----------------------------  ----------------\n"
        "   7   3                       FC FR FL\n"
        " 263   4  BC                   FC FR FL  like Dpl I\n"
        " 271   5  BC                LF FC FR FL\n"
        "  59   5              BR BL LF    FR FL\n");
    exit(1);
}

static uint32_t numofbits(uint32_t bits) {
    bits = (bits & 0x55555555) + (bits >> 1 & 0x55555555);
    bits = (bits & 0x33333333) + (bits >> 2 & 0x33333333);
    bits = (bits & 0x0f0f0f0f) + (bits >> 4 & 0x0f0f0f0f);
    bits = (bits & 0x00ff00ff) + (bits >> 8 & 0x00ff00ff);
    return (bits & 0x0000ffff) + (bits >>16 & 0x0000ffff);
}