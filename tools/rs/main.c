#include <stdio.h>
#include <string.h>

#include "constants.h"
#include "verify.h"

typedef int (*command_fn)(char **argv);

typedef struct {
    const char *name;
    int expected_argc;
    const char *usage;
    command_fn run;
} command_t;

static int run_gf_256_field_info(char **argv);
static int run_generate_reed_solomon_constants(char **argv);
static int run_verify_repo(char **argv);
static void print_top_level_usage(const char *program_name);
static void print_command_usage(const char *program_name, const command_t *command);
static const parameter_set_t *require_parameter_set(const char *name);

static const command_t COMMANDS[] = {
    {"gf-256-field-info", 2, "gf-256-field-info", run_gf_256_field_info},
    {"generate_reed_solomon_constants", 3, "generate_reed_solomon_constants <hqc-1|hqc-3|hqc-5>",
     run_generate_reed_solomon_constants},
    {"verify-repo", 3, "verify-repo <hqc-1|hqc-3|hqc-5>", run_verify_repo},
};

int main(int argc, char **argv) {
    size_t command_count = sizeof(COMMANDS) / sizeof(COMMANDS[0]);

    if (argc < 2) {
        print_top_level_usage(argv[0]);
        return 1;
    }

    for (size_t index = 0; index < command_count; ++index) {
        const command_t *command = &COMMANDS[index];

        if (strcmp(argv[1], command->name) != 0) {
            continue;
        }

        if (argc != command->expected_argc) {
            print_command_usage(argv[0], command);
            return 1;
        }

        return command->run(argv);
    }

    fprintf(stderr, "unknown command '%s'\n", argv[1]);
    print_top_level_usage(argv[0]);
    return 1;
}

static int run_gf_256_field_info(char **argv) {
    (void)argv;
    return print_gf_256_field_info();
}

static int run_generate_reed_solomon_constants(char **argv) {
    const parameter_set_t *parameter_set = require_parameter_set(argv[2]);
    rs_constants_t constants;

    if (parameter_set == NULL) {
        return 1;
    }

    build_rs_constants(parameter_set, &constants);
    print_parameter_set_constants(&constants);
    return 0;
}

static int run_verify_repo(char **argv) {
    const parameter_set_t *parameter_set = require_parameter_set(argv[2]);
    rs_constants_t constants;

    if (parameter_set == NULL) {
        return 1;
    }

    build_rs_constants(parameter_set, &constants);
    return verify_parameter_set_constants(&constants);
}

static void print_top_level_usage(const char *program_name) {
    fprintf(stderr, "usage: %s <gf-256-field-info|generate_reed_solomon_constants|verify-repo>\n", program_name);
}

static void print_command_usage(const char *program_name, const command_t *command) {
    fprintf(stderr, "usage: %s %s\n", program_name, command->usage);
}

static const parameter_set_t *require_parameter_set(const char *name) {
    const parameter_set_t *parameter_set = find_parameter_set(name);

    if (parameter_set == NULL) {
        fprintf(stderr, "unknown parameter set '%s', expected one of: %s\n", name, supported_parameter_sets());
    }

    return parameter_set;
}
