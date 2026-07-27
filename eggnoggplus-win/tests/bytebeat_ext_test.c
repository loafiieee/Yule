#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../bytebeat_ext.h"

static void require(int condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static uint16_t read_u16_le(const unsigned char* value) {
    return (uint16_t)((uint16_t)value[0] | ((uint16_t)value[1] << 8));
}

static uint32_t read_u32_le(const unsigned char* value) {
    return (uint32_t)value[0] |
           ((uint32_t)value[1] << 8) |
           ((uint32_t)value[2] << 16) |
           ((uint32_t)value[3] << 24);
}

static double evaluate(const char* expression, BytebeatMode mode,
                       uint32_t t, uint32_t sample_rate) {
    BytebeatProgram program;
    BytebeatDiagnostic diagnostic;
    double value = 0.0;
    if (!bytebeat_compile(expression, mode, &program, &diagnostic)) {
        fprintf(stderr, "compile failed at %lu: %s\n",
                (unsigned long)diagnostic.offset, diagnostic.message);
        exit(1);
    }
    require(bytebeat_evaluate(&program, t, sample_rate, &value),
            "compiled expression should evaluate");
    return value;
}

static void test_js_expression_surface(void) {
    require(evaluate("t", BYTEBEAT_MODE_U8, 42u, 8000u) == 42.0,
            "t should expose the sample index");
    require(evaluate("sr + sampleRate + sample_rate", BYTEBEAT_MODE_U8,
                     0u, 8000u) == 24000.0,
            "sample-rate aliases should work");
    require(fabs(evaluate("time + seconds", BYTEBEAT_MODE_FLOAT,
                          4000u, 8000u) - 1.0) < 1e-12,
            "seconds aliases should expose continuous time");
    require(evaluate("0xff & (t << 2)", BYTEBEAT_MODE_U8,
                     3u, 8000u) == 12.0,
            "hex and JavaScript-style bit operators should work");
    require(evaluate("0xffffffff >>> 24", BYTEBEAT_MODE_U8,
                     0u, 8000u) == 255.0,
            "unsigned right shift should work");
    require(evaluate("t >= 4 && t < 8 ? 17 : 3", BYTEBEAT_MODE_U8,
                     6u, 8000u) == 17.0,
            "comparisons, logical operators, and ternary should work");
    require(evaluate("t >= 4 && t < 8 ? 17 : 3", BYTEBEAT_MODE_U8,
                     9u, 8000u) == 3.0,
            "conditional false branch should work");
    require(evaluate("2 ** 3 ** 2", BYTEBEAT_MODE_FLOAT,
                     0u, 8000u) == 512.0,
            "power should be right associative");
    require(fabs(evaluate("Math.sin(Math.PI / 2)", BYTEBEAT_MODE_FLOAT,
                          0u, 8000u) - 1.0) < 1e-12,
            "Math-style functions/constants should work");
    require(evaluate("clamp(-2, -1, 1) + max(2, 4) + min(5, 3)",
                     BYTEBEAT_MODE_FLOAT, 0u, 8000u) == 6.0,
            "bounded multi-argument authoring helpers should work");
    require(evaluate("fract(2.75) + trunc(-2.75)",
                     BYTEBEAT_MODE_FLOAT, 0u, 8000u) == -1.25,
            "fract and trunc should work");
    {
        double first = evaluate("noise(t)", BYTEBEAT_MODE_FLOAT, 42u, 8000u);
        double again = evaluate("noise(t)", BYTEBEAT_MODE_FLOAT, 42u, 8000u);
        double next = evaluate("noise(t)", BYTEBEAT_MODE_FLOAT, 43u, 8000u);
        require(first == again,
                "noise must be deterministic for a sample index");
        require(first >= 0.0 && first < 1.0 &&
                next >= 0.0 && next < 1.0 && first != next,
                "noise must vary inside the documented unit interval");
    }
}

static void test_diagnostics_and_limits(void) {
    BytebeatProgram program;
    BytebeatDiagnostic diagnostic;
    char too_long[BYTEBEAT_MAX_EXPRESSION + 2u];
    memset(too_long, '1', sizeof(too_long) - 1u);
    too_long[sizeof(too_long) - 1u] = '\0';

    require(!bytebeat_compile("t + )", BYTEBEAT_MODE_U8,
                              &program, &diagnostic),
            "invalid expression should fail");
    require(diagnostic.message[0] != '\0' && diagnostic.offset >= 3u,
            "syntax failure should report a byte offset and message");
    require(!bytebeat_compile("system('nope')", BYTEBEAT_MODE_U8,
                              &program, &diagnostic),
            "unknown functions should fail closed");
    require(strstr(diagnostic.message, "unknown") != NULL,
            "unknown-function diagnostic should be useful");
    require(!bytebeat_compile(too_long, BYTEBEAT_MODE_U8,
                              &program, &diagnostic),
            "overlong expression should fail before parsing");
    require(strstr(diagnostic.message, "1024") != NULL,
            "length diagnostic should state the limit");
}

static void test_render_modes(void) {
    BytebeatProgram program;
    BytebeatDiagnostic diagnostic;
    BytebeatRenderOptions options;
    int16_t samples[8];

    require(bytebeat_compile("t", BYTEBEAT_MODE_U8,
                             &program, &diagnostic),
            "basic bytebeat should compile");
    memset(&options, 0, sizeof(options));
    options.sample_rate = 8000u;
    options.sample_count = 4u;
    options.gain = 1.0;
    require(bytebeat_render_pcm16(&program, &options, samples, 8u,
                                  &diagnostic),
            "bytebeat should render");
    require(samples[0] == -32768 && samples[1] == -32512 &&
            samples[2] == -32256 && samples[3] == -32000,
            "u8 bytebeat should map exactly to signed 16-bit PCM");

    require(bytebeat_compile("sin(2*pi*t/sr)", BYTEBEAT_MODE_FLOAT,
                             &program, &diagnostic),
            "floatbeat should compile");
    options.sample_rate = 8000u;
    options.sample_count = 5u;
    options.gain = 1.0;
    options.fade_samples = 0u;
    require(bytebeat_render_pcm16(&program, &options, samples, 8u,
                                  &diagnostic),
            "floatbeat should render");
    require(samples[0] == 0 && samples[1] > 20 && samples[1] < 30,
            "floatbeat should map normalized math output to PCM");

    options.sample_count = 8u;
    options.fade_samples = 2u;
    require(bytebeat_compile("1", BYTEBEAT_MODE_FLOAT,
                             &program, &diagnostic),
            "fade fixture should compile");
    require(bytebeat_render_pcm16(&program, &options, samples, 8u,
                                  &diagnostic),
            "fade fixture should render");
    require(samples[0] == 0 && samples[1] > 16000 &&
            samples[2] == 32767 && samples[6] > 16000 &&
            samples[7] == 0,
            "click-suppression fade should affect both ends");
}

static void test_render_resource_limits(void) {
    BytebeatProgram program;
    BytebeatDiagnostic diagnostic;
    BytebeatRenderOptions options;
    int16_t sample = 0;
    require(bytebeat_compile("t", BYTEBEAT_MODE_U8,
                             &program, &diagnostic),
            "limit fixture should compile");
    memset(&options, 0, sizeof(options));
    options.sample_rate = 3999u;
    options.sample_count = 1u;
    options.gain = 1.0;
    require(!bytebeat_render_pcm16(&program, &options, &sample, 1u,
                                   &diagnostic),
            "too-low sample rate should fail");
    options.sample_rate = 8000u;
    options.sample_count = BYTEBEAT_MAX_SAMPLES + 1u;
    require(!bytebeat_render_pcm16(&program, &options, &sample, 1u,
                                   &diagnostic),
            "oversized render should fail before touching output");
    options.sample_count = 1u;
    options.gain = 5.0;
    require(!bytebeat_render_pcm16(&program, &options, &sample, 1u,
                                   &diagnostic),
            "excessive gain should fail");
}

static void test_wav(void) {
    BytebeatProgram program;
    BytebeatDiagnostic diagnostic;
    BytebeatRenderOptions options;
    unsigned char wav[60];
    size_t wav_size = 0u;
    require(bytebeat_compile("128", BYTEBEAT_MODE_U8,
                             &program, &diagnostic),
            "WAV fixture should compile");
    memset(&options, 0, sizeof(options));
    options.sample_rate = 11025u;
    options.sample_count = 8u;
    options.gain = 1.0;
    require(bytebeat_wav_size(8u) == sizeof(wav),
            "WAV size should be exact");
    require(bytebeat_render_wav(&program, &options, wav, sizeof(wav),
                                &wav_size, &diagnostic),
            "WAV should render");
    require(wav_size == sizeof(wav), "reported WAV size should be exact");
    require(memcmp(wav, "RIFF", 4u) == 0 &&
            memcmp(wav + 8u, "WAVEfmt ", 8u) == 0 &&
            memcmp(wav + 36u, "data", 4u) == 0,
            "WAV chunk identifiers should be canonical");
    require(read_u32_le(wav + 4u) == 52u &&
            read_u32_le(wav + 24u) == 11025u &&
            read_u32_le(wav + 28u) == 22050u &&
            read_u32_le(wav + 40u) == 16u,
            "WAV sizes and rates should be exact");
    require(read_u16_le(wav + 20u) == 1u &&
            read_u16_le(wav + 22u) == 1u &&
            read_u16_le(wav + 34u) == 16u,
            "WAV should be mono 16-bit PCM");
    require(read_u16_le(wav + 44u) == 0u,
            "centered bytebeat value should render silence");
}

static void test_playlist_marker(void) {
    BytebeatDiagnostic diagnostic;
    BytebeatPlaylistOptions options;
    uint32_t sample_rate = 0u;
    double volume = 0.0;
    require(bytebeat_parse_playlist_marker(
                "( yule:bytebeat )", &sample_rate, &volume, &diagnostic),
            "bare playlist marker should use defaults");
    require(sample_rate == BYTEBEAT_DEFAULT_PLAYLIST_SAMPLE_RATE &&
            volume == BYTEBEAT_DEFAULT_PLAYLIST_VOLUME,
            "missing playlist options should receive documented defaults");

    require(bytebeat_parse_playlist_marker(
                "( yule:bytebeat volume=0.5 sample_rate=44100 )",
                &sample_rate, &volume, &diagnostic),
            "explicit playlist rate and volume should parse in either order");
    require(sample_rate == 44100u && fabs(volume - 0.5) < 1e-12,
            "explicit playlist options should apply exactly");

    require(!bytebeat_parse_playlist_marker(
                "( yule:bytebeat sample_rate=48001 )",
                &sample_rate, &volume, &diagnostic),
            "out-of-range playlist rate should fail closed");
    require(strstr(diagnostic.message, "4000") != NULL,
            "invalid playlist rate should report its range");
    require(!bytebeat_parse_playlist_marker(
                "( yule:bytebeat sample_rate=44100oops )",
                &sample_rate, &volume, &diagnostic),
            "playlist rate suffix garbage should fail closed");
    require(!bytebeat_parse_playlist_marker(
                "( yule:bytebeat mystery=1 )",
                &sample_rate, &volume, &diagnostic),
            "unknown playlist options should fail closed");
    require(bytebeat_parse_playlist_options(
                "( yule:bytebeat mode=signed-bytebeat engine=dollchan )",
                &options, &diagnostic),
            "Dollchan engine and mode should parse in either order");
    require(options.engine == BYTEBEAT_PLAYLIST_DOLLCHAN &&
            options.mode == BYTEBEAT_PLAYLIST_S8 &&
            options.sample_rate == BYTEBEAT_DEFAULT_PLAYLIST_SAMPLE_RATE &&
            options.output_rate ==
                BYTEBEAT_DEFAULT_PLAYLIST_OUTPUT_RATE,
            "Dollchan options should preserve omitted defaults");
    require(bytebeat_parse_playlist_options(
                "( yule:bytebeat sample_rate=32000 output_rate=44100 )",
                &options, &diagnostic) &&
            options.sample_rate == 32000u &&
            options.output_rate == 44100u,
            "formula and mixer rates should be independently configurable");
    require(!bytebeat_parse_playlist_options(
                "( yule:bytebeat output_rate=7999 )",
                &options, &diagnostic),
            "out-of-range mixer output rate should fail closed");
    require(bytebeat_parse_playlist_options(
                "( yule:bytebeat engine=javascript mode=funcbeat )",
                &options, &diagnostic) &&
            options.engine == BYTEBEAT_PLAYLIST_DOLLCHAN &&
            options.mode == BYTEBEAT_PLAYLIST_FUNC,
            "JavaScript alias and funcbeat mode should be accepted");
    require(!bytebeat_parse_playlist_options(
                "( yule:bytebeat engine=dollchan engine=bounded )",
                &options, &diagnostic),
            "duplicate engine option should fail closed");
    require(!bytebeat_parse_playlist_options(
                "( yule:bytebeat mode=unknown )",
                &options, &diagnostic),
            "unknown playback mode should fail closed");
}

static void test_playlist_fixture(void) {
    FILE* file = fopen("data/tune8.txt", "rb");
    char line[1200];
    int found_marker = 0;
    BytebeatPlaylistOptions options;
    BytebeatDiagnostic diagnostic;
    require(file != NULL, "playlist fixture should exist");
    while (fgets(line, sizeof(line), file)) {
        size_t length;
        if (!found_marker) {
            if (strstr(line, "yule:bytebeat")) {
                require(bytebeat_parse_playlist_options(
                            line, &options, &diagnostic),
                        "shipped playlist marker should parse");
                require(options.sample_rate == 44100u &&
                        options.engine == BYTEBEAT_PLAYLIST_DOLLCHAN &&
                        options.mode == BYTEBEAT_PLAYLIST_U8,
                        "Steady On Tim should request Dollchan bytebeat at 44100 Hz");
                found_marker = 1;
            }
            continue;
        }
        length = strlen(line);
        while (length > 0u &&
               (line[length - 1u] == '\r' || line[length - 1u] == '\n')) {
            line[--length] = '\0';
        }
        if (length == 0u) continue;
        require(strstr(line, "=>") != NULL &&
                strstr(line, "random()") != NULL,
                "shipped track should retain the original Dollchan JavaScript");
        fclose(file);
        return;
    }
    fclose(file);
    require(0, "bounded playlist fixture should contain an expression");
}

int main(void) {
    test_js_expression_surface();
    test_diagnostics_and_limits();
    test_render_modes();
    test_render_resource_limits();
    test_wav();
    test_playlist_marker();
    test_playlist_fixture();
    puts("bytebeat expression/render tests: OK");
    return 0;
}
