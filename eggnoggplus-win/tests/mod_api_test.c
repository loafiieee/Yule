#include "../mod_api.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); \
        ++failures; \
    } \
} while (0)

static void set_requirement(
    char requirements[][MOD_API_CAPABILITY_NAME_MAX],
    int index,
    const char* value
) {
    snprintf(requirements[index], MOD_API_CAPABILITY_NAME_MAX, "%s", value);
}

int main(void) {
    char error[256];
    char requirements[MOD_API_REQUIRED_CAPABILITIES_MAX]
                     [MOD_API_CAPABILITY_NAME_MAX] = {{0}};
    int i;

    CHECK(MOD_API_MAJOR == 1);
    CHECK(MOD_API_REVISION >= 4);
    CHECK(mod_api_capability_count() >= 10);
    CHECK(mod_api_has_capability("events.removable"));
    CHECK(mod_api_has_capability("http.response_metadata"));

    for (i = 0; i < mod_api_capability_count(); ++i) {
        const char* capability = mod_api_capability_at(i);
        CHECK(capability != NULL);
        CHECK(mod_api_capability_name_valid(capability));
        CHECK(mod_api_has_capability(capability));
        if (i > 0) {
            CHECK(strcmp(mod_api_capability_at(i - 1), capability) < 0);
        }
    }
    CHECK(mod_api_capability_at(-1) == NULL);
    CHECK(mod_api_capability_at(mod_api_capability_count()) == NULL);

    CHECK(!mod_api_capability_name_valid(NULL));
    CHECK(!mod_api_capability_name_valid(""));
    CHECK(!mod_api_capability_name_valid("FS.pick_file"));
    CHECK(!mod_api_capability_name_valid("fs/pick_file"));
    CHECK(!mod_api_capability_name_valid(".fs"));
    CHECK(!mod_api_has_capability("not.real"));

    CHECK(mod_api_check_compatibility(MOD_API_MAJOR, 0, NULL, 0,
                                      error, sizeof(error)));
    CHECK(error[0] == '\0');
    CHECK(mod_api_check_compatibility(MOD_API_MAJOR, MOD_API_REVISION,
                                      NULL, 0, error, sizeof(error)));

    CHECK(!mod_api_check_compatibility(MOD_API_MAJOR + 1, 0, NULL, 0,
                                       error, sizeof(error)));
    CHECK(strstr(error, "major") != NULL);
    CHECK(!mod_api_check_compatibility(MOD_API_MAJOR, -1, NULL, 0,
                                       error, sizeof(error)));
    CHECK(strstr(error, "zero") != NULL);
    CHECK(!mod_api_check_compatibility(MOD_API_MAJOR, MOD_API_REVISION + 1,
                                       NULL, 0, error, sizeof(error)));
    CHECK(strstr(error, "revision") != NULL);

    set_requirement(requirements, 0, "fs.pick_file");
    set_requirement(requirements, 1, "storage.v1");
    CHECK(mod_api_check_compatibility(MOD_API_MAJOR, 1, &requirements[0][0], 2,
                                      error, sizeof(error)));

    set_requirement(requirements, 1, "missing.capability");
    CHECK(!mod_api_check_compatibility(MOD_API_MAJOR, 1, &requirements[0][0], 2,
                                       error, sizeof(error)));
    CHECK(strstr(error, "missing.capability") != NULL);

    set_requirement(requirements, 0, "Bad Capability");
    CHECK(!mod_api_check_compatibility(MOD_API_MAJOR, 1, &requirements[0][0], 1,
                                       error, sizeof(error)));
    CHECK(strstr(error, "valid capability") != NULL);

    CHECK(!mod_api_check_compatibility(
        MOD_API_MAJOR, 0, &requirements[0][0],
        MOD_API_REQUIRED_CAPABILITIES_MAX + 1, error, sizeof(error)));
    CHECK(!mod_api_check_compatibility(MOD_API_MAJOR, 0, NULL, 1,
                                       error, sizeof(error)));

    if (failures != 0) {
        fprintf(stderr, "mod API compatibility tests failed: %d\n", failures);
        return 1;
    }
    puts("mod API compatibility tests: OK");
    return 0;
}
