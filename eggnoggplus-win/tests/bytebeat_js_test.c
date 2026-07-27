#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "../bytebeat_ext.h"
#include "../bytebeat_js.h"

static void require(int condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static int16_t reference_pcm_u8(unsigned int byte_value, double volume) {
    double normalized = (double)(byte_value & 255u) / 127.5 - 1.0;
    double sample = normalized * volume * 32767.0;
    return (int16_t)(sample < 0.0
        ? ceil(sample - 0.5) : floor(sample + 0.5));
}

static char* load_playlist_track(int index, char* marker,
                                 size_t marker_size,
                                 size_t* out_source_size) {
    char path[64];
    FILE* file;
    char* file_data;
    char* marker_start;
    char* marker_end;
    char* source_start;
    char* source_end;
    char* source;
    long file_size_long;
    size_t file_size;
    size_t marker_length;
    size_t source_length;

    snprintf(path, sizeof(path), "data/tune%d.txt", index);
    file = fopen(path, "rb");
    require(file != NULL, "playlist fixture should exist");
    require(fseek(file, 0, SEEK_END) == 0,
            "playlist fixture should be seekable");
    file_size_long = ftell(file);
    require(file_size_long > 0 && fseek(file, 0, SEEK_SET) == 0,
            "playlist fixture should have a readable size");
    file_size = (size_t)file_size_long;
    file_data = (char*)malloc(file_size + 1u);
    require(file_data != NULL, "playlist fixture allocation should succeed");
    require(fread(file_data, 1u, file_size, file) == file_size,
            "playlist fixture should read completely");
    fclose(file);
    file_data[file_size] = '\0';
    require(memchr(file_data, '\0', file_size) == NULL,
            "playlist fixture should not contain NUL bytes");

    marker[0] = '\0';
    marker_start = strstr(file_data, "yule:bytebeat");
    require(marker_start != NULL,
            "playlist fixture should contain a bytebeat marker");
    while (marker_start > file_data && marker_start[-1] != '\n') {
        marker_start--;
    }
    marker_end = strchr(marker_start, '\n');
    require(marker_end != NULL,
            "playlist marker should be followed by source");
    while (marker_end > marker_start &&
           (marker_end[-1] == '\r' || marker_end[-1] == ' ' ||
            marker_end[-1] == '\t')) {
        marker_end--;
    }
    marker_length = (size_t)(marker_end - marker_start);
    require(marker_length + 1u <= marker_size,
            "playlist marker should fit the test buffer");
    memcpy(marker, marker_start, marker_length);
    marker[marker_length] = '\0';

    source_start = strchr(marker_start, '\n') + 1;
    source_end = file_data + file_size;
    while (source_start < source_end &&
           (*source_start == ' ' || *source_start == '\t' ||
            *source_start == '\r' || *source_start == '\n')) {
        source_start++;
    }
    while (source_end > source_start &&
           (source_end[-1] == ' ' || source_end[-1] == '\t' ||
            source_end[-1] == '\r' || source_end[-1] == '\n')) {
        source_end--;
    }
    source_length = (size_t)(source_end - source_start);
    require(source_length > 0u,
            "playlist fixture should contain JavaScript source");
    source = (char*)malloc(source_length + 1u);
    require(source != NULL,
            "playlist source allocation should succeed");
    memcpy(source, source_start, source_length);
    source[source_length] = '\0';
    free(file_data);
    if (out_source_size) *out_source_size = source_length;
    return source;
}

static void test_shipped_dollchan_track(void) {
    static const struct {
        uint64_t t;
        unsigned int byte_value;
    } reference[] = {
        { 0u, 0u },
        { 13u, 42u },
        { 47u, 68u },
        { 48u, 42u },
        { 51u, 0u },
        { 999u, 25u },
        { 15000u, 25u },
        { 32000u, 25u },
        { 639999u, 42u }
    };
    BytebeatPlaylistOptions options;
    BytebeatDiagnostic diagnostic;
    BytebeatJsRuntime* runtime;
    char marker[1024];
    char* source;
    size_t source_length;
    char error[256];
    size_t i;
    source = load_playlist_track(8, marker, sizeof(marker),
                                 &source_length);
    require(bytebeat_parse_playlist_options(
                marker, &options, &diagnostic),
            "Dollchan playlist marker should parse");
    require(options.engine == BYTEBEAT_PLAYLIST_DOLLCHAN &&
            options.mode == BYTEBEAT_PLAYLIST_U8 &&
            options.sample_rate == 44100u,
            "tune8 should declare Dollchan bytebeat at 44100 Hz");
    runtime = bytebeat_js_create(
        source, source_length, BYTEBEAT_JS_MODE_U8,
        options.sample_rate, 1.0, error, sizeof(error));
    if (!runtime) {
        fprintf(stderr, "tune8 compile failed: %s\n", error);
        require(0, "original Dollchan tune8 formula should compile");
    }
    bytebeat_js_begin_callback(runtime, 500u);
    for (i = 0u; i < sizeof(reference) / sizeof(reference[0]); i++) {
        int16_t left = 0;
        int16_t right = 0;
        require(bytebeat_js_sample(runtime, reference[i].t,
                                   &left, &right),
                "Dollchan reference sample should evaluate");
        require(left == right, "mono Dollchan formula should duplicate channels");
        require(left == reference_pcm_u8(reference[i].byte_value, 1.0),
                "tune8 sample must match Dollchan's JavaScript byte output");
    }
    bytebeat_js_end_callback(runtime);
    require(!bytebeat_js_failed(runtime),
            "reference track should remain healthy");
    bytebeat_js_destroy(runtime);
    free(source);
}

static BytebeatJsMode playlist_js_mode(BytebeatPlaylistMode mode) {
    switch (mode) {
        case BYTEBEAT_PLAYLIST_S8: return BYTEBEAT_JS_MODE_S8;
        case BYTEBEAT_PLAYLIST_FLOAT: return BYTEBEAT_JS_MODE_FLOAT;
        case BYTEBEAT_PLAYLIST_FUNC: return BYTEBEAT_JS_MODE_FUNC;
        case BYTEBEAT_PLAYLIST_U8:
        default:
            return BYTEBEAT_JS_MODE_U8;
    }
}

static void test_editable_complex_tracks(void) {
    static const int tracks[] = { 9, 10 };
    int16_t output[BYTEBEAT_JS_BATCH_FRAMES * 2u];
    size_t i;

    for (i = 0u; i < sizeof(tracks) / sizeof(tracks[0]); i++) {
        BytebeatPlaylistOptions options;
        BytebeatDiagnostic diagnostic;
        BytebeatJsRuntime* runtime;
        char marker[1024];
        char* source;
        size_t source_length;
        char error[256];
        ULONGLONG started;
        ULONGLONG first_elapsed;
        ULONGLONG second_elapsed;
        uint32_t budget;

        source = load_playlist_track(tracks[i], marker,
                                     sizeof(marker), &source_length);
        require(bytebeat_parse_playlist_options(
                    marker, &options, &diagnostic),
                "complex Dollchan playlist marker should parse");
        require(options.engine == BYTEBEAT_PLAYLIST_DOLLCHAN,
                "editable complex track should select the Dollchan engine");
        runtime = bytebeat_js_create(
            source, source_length, playlist_js_mode(options.mode),
            options.sample_rate, options.volume, error, sizeof(error));
        if (!runtime) {
            fprintf(stderr, "tune%d compile failed: %s\n",
                    tracks[i], error);
            require(0, "shipped complex Dollchan track should compile");
        }
        budget = bytebeat_js_render_budget_ms(
            BYTEBEAT_JS_BATCH_FRAMES, options.sample_rate);
        require(budget >= BYTEBEAT_JS_CALLBACK_BUDGET_MS &&
                budget <= BYTEBEAT_JS_CALLBACK_MAX_BUDGET_MS,
                "block render budget should remain strictly bounded");
        started = GetTickCount64();
        bytebeat_js_begin_callback(runtime, budget);
        require(bytebeat_js_render(
                    runtime, 0u, BYTEBEAT_JS_BATCH_FRAMES, output),
                "complex Dollchan track should render a full block");
        bytebeat_js_end_callback(runtime);
        first_elapsed = GetTickCount64() - started;
        started = GetTickCount64();
        bytebeat_js_begin_callback(runtime, budget);
        require(bytebeat_js_render(
                    runtime, BYTEBEAT_JS_BATCH_FRAMES,
                    BYTEBEAT_JS_BATCH_FRAMES, output),
                "warmed Dollchan track should render its next full block");
        bytebeat_js_end_callback(runtime);
        second_elapsed = GetTickCount64() - started;
        printf("tune%d mode=%d rate=%lu block render: %llu/%llu ms "
               "(budget %u ms)\n",
               tracks[i], (int)options.mode,
               (unsigned long)options.sample_rate,
               (unsigned long long)first_elapsed,
               (unsigned long long)second_elapsed,
               (unsigned)budget);
        require(!bytebeat_js_failed(runtime),
                "complex Dollchan block renderer should remain healthy");
        bytebeat_js_destroy(runtime);
        free(source);
    }
}

static void test_large_source_limit(void) {
    static const char expression[] = "t&255";
    const size_t padding_size = 70u * 1024u;
    const size_t source_length = padding_size + sizeof(expression) - 1u;
    char* source = (char*)malloc(source_length + 1u);
    BytebeatJsRuntime* runtime;
    char error[256];
    int16_t left = 0;
    int16_t right = 0;

    require(source != NULL, "large-source fixture allocation should succeed");
    memset(source, ' ', padding_size);
    memcpy(source + padding_size, expression, sizeof(expression));
    runtime = bytebeat_js_create(
        source, source_length, BYTEBEAT_JS_MODE_U8,
        8000u, 1.0, error, sizeof(error));
    if (!runtime) {
        fprintf(stderr, "large source compile failed: %s\n", error);
        require(0, "JavaScript source above the former 64 KiB limit should compile");
    }
    bytebeat_js_begin_callback(runtime, 100u);
    require(bytebeat_js_sample(runtime, 7u, &left, &right),
            "large-source track should evaluate");
    require(left == reference_pcm_u8(7u, 1.0) && left == right,
            "large-source padding must not change formula output");
    bytebeat_js_end_callback(runtime);
    bytebeat_js_destroy(runtime);
    free(source);
}

static void test_dollchan_modes_and_stereo(void) {
    BytebeatJsRuntime* runtime;
    char error[256];
    int16_t left;
    int16_t right;

    runtime = bytebeat_js_create(
        "voice=x=>x&255,[voice(t),255-voice(t)]",
        strlen("voice=x=>x&255,[voice(t),255-voice(t)]"),
        BYTEBEAT_JS_MODE_U8, 8000u, 1.0, error, sizeof(error));
    require(runtime != NULL, "arrow function and stereo array should compile");
    bytebeat_js_begin_callback(runtime, 100u);
    require(bytebeat_js_sample(runtime, 7u, &left, &right),
            "stereo bytebeat should evaluate");
    require(left == reference_pcm_u8(7u, 1.0) &&
            right == reference_pcm_u8(248u, 1.0),
            "stereo array channels should use Dollchan bytebeat mapping");
    bytebeat_js_end_callback(runtime);
    bytebeat_js_destroy(runtime);

    runtime = bytebeat_js_create(
        "0", 1u, BYTEBEAT_JS_MODE_S8,
        8000u, 1.0, error, sizeof(error));
    require(runtime != NULL, "signed bytebeat should compile");
    bytebeat_js_begin_callback(runtime, 100u);
    require(bytebeat_js_sample(runtime, 0u, &left, &right),
            "signed bytebeat should evaluate");
    require(left == reference_pcm_u8(128u, 1.0) && left == right,
            "signed bytebeat should add 128 before wrapping");
    bytebeat_js_end_callback(runtime);
    bytebeat_js_destroy(runtime);

    runtime = bytebeat_js_create(
        "[.5,-.25]", strlen("[.5,-.25]"), BYTEBEAT_JS_MODE_FLOAT,
        44100u, 1.0, error, sizeof(error));
    require(runtime != NULL, "floatbeat stereo should compile");
    bytebeat_js_begin_callback(runtime, 100u);
    require(bytebeat_js_sample(runtime, 0u, &left, &right),
            "floatbeat stereo should evaluate");
    require(left == 16384 && right == -8192,
            "floatbeat should clamp and map normalized channels");
    bytebeat_js_end_callback(runtime);
    bytebeat_js_destroy(runtime);

    runtime = bytebeat_js_create(
        "return (time,sampleRate)=>sin(2*PI*440*time)",
        strlen("return (time,sampleRate)=>sin(2*PI*440*time)"),
        BYTEBEAT_JS_MODE_FUNC, 44100u, 1.0, error, sizeof(error));
    require(runtime != NULL, "funcbeat factory should return a function");
    bytebeat_js_begin_callback(runtime, 100u);
    require(bytebeat_js_sample(runtime, 0u, &left, &right) &&
            left == 0 && right == 0,
            "funcbeat should receive seconds and sample rate");
    bytebeat_js_end_callback(runtime);
    bytebeat_js_destroy(runtime);
}

static void test_isolation_and_limits(void) {
    BytebeatJsRuntime* runtime;
    char error[256];
    int16_t left;
    int16_t right;
    runtime = bytebeat_js_create(
        "typeof std==='undefined'&&typeof os==='undefined'",
        strlen("typeof std==='undefined'&&typeof os==='undefined'"),
        BYTEBEAT_JS_MODE_U8, 8000u, 1.0, error, sizeof(error));
    require(runtime != NULL, "isolation probe should compile");
    bytebeat_js_begin_callback(runtime, 100u);
    require(bytebeat_js_sample(runtime, 0u, &left, &right),
            "isolation probe should evaluate");
    require(left == reference_pcm_u8(1u, 1.0),
            "std and os host modules must not exist");
    bytebeat_js_end_callback(runtime);
    bytebeat_js_destroy(runtime);

    runtime = bytebeat_js_create(
        "t+)", strlen("t+)"), BYTEBEAT_JS_MODE_U8,
        8000u, 1.0, error, sizeof(error));
    require(runtime == NULL && strstr(error, "compile error") != NULL,
            "syntax errors should fail with useful diagnostics");

    runtime = bytebeat_js_create(
        "(()=>{for(;;);})()", strlen("(()=>{for(;;);})()"),
        BYTEBEAT_JS_MODE_U8, 8000u, 1.0, error, sizeof(error));
    require(runtime == NULL,
            "runaway validation code should be interrupted");
}

int main(void) {
    test_shipped_dollchan_track();
    test_editable_complex_tracks();
    test_large_source_limit();
    test_dollchan_modes_and_stereo();
    test_isolation_and_limits();
    puts("sandboxed Dollchan JavaScript tests: OK");
    return 0;
}
