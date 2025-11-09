#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
//#define DEBUG_PRINTS
#include <string.h>
// #include "error_debug.h"
// #include "logger.h"
#include "argvProcessor.h"

#ifndef FREE
#define FREE(ptr) do {free(ptr); ptr = NULL;} while (0)
#endif

static flagDescriptor_t flagsDescriptions[MAX_REGISTERED_FLAGS] = {};
static size_t registeredFlagsCount_ = 0;
static const char *defaultArgs[MAX_DEFAULT_ARGS] = {};
static size_t defaultArgsCount_ = 0;
static FlagsHolder_t flags = {};
static const char* helpMessageHeader_ = NULL;
static bool helpMessageEnabled = false;

/*!
    @brief Scan argument in full form (--encode)

    @param remainToScan [in] Number of arguments that were'nt already scanned
    @param argv [in] Current argv position

    @return Number of arguments remained to scan. Can return int < 0, is something goes wrong

    Counts current argument as processed only after next argument was processed by scanToFlag() function
*/
static int scanFullArgument(int remainToScan, const char *argv[]);

/*!
    @brief Scan argument in short form (-eio)

    @param remainToScan [in] Number of arguments that weren't already scanned
    @param argv [in] Current argv position

    @return Number of arguments remained to scan. Can return int < 0, is something goes wrong

    Counts current argument as processed only after all next argument were processed by scanToFlag() function

*/
static int scanShortArguments(int remainToScan, const char *argv[]);

/*!
    @brief Scan value to flag

    @param flag [out] Flag to write value
    @param remainToScan Number of arguments that weren't scanned
    @param argv [in] Current argv position

    @return Number of arguments remained to scan. Can return int < 0, is something goes wrong

    Decreases remainToScan by number of elements it processed (typically 0 or 1)

    argv must point to value, that should be scanned to flag
*/
static int scanToFlag(flagDescriptor_t desc, int remainToScan, const char *argv[]);

static flagVal_t *findFlag(const char *flagName);
static enum argvStatus addFlag(flagDescriptor_t desc, fVal_t val);

/*
    @brief concatenate strings with given separator string
*/
static char* joinStrings(const char **strings, size_t len, const char *separator);

enum argvStatus setHelpMessageHeader(const char* header) {
    // MY_ASSERT(header, abort());
    helpMessageHeader_ = header;
    return ARGV_SUCCESS;
}

enum argvStatus enableHelpFlag(const char *header) {
    // MY_ASSERT(header, abort());
    enum argvStatus result = registerFlag(TYPE_BLANK, "-h", "--help", "Prints help message");
    if (result != ARGV_SUCCESS) return result;
    result = setHelpMessageHeader(header);
    helpMessageEnabled = true;
    return result;
}

enum argvStatus registerFlag(enum flagType type,
                         const char* shortName,
                         const char* fullName,
                         const char* helpMessage) {
    flagDescriptor_t flagInfo = {type, shortName, fullName, helpMessage};
    if (registeredFlagsCount_ < MAX_REGISTERED_FLAGS) {
        flagsDescriptions[registeredFlagsCount_++] = flagInfo;
        return ARGV_SUCCESS;
    } else
        return ARGV_ERROR;
}

const char *getDefaultArgument(size_t idx) {
    if (idx < defaultArgsCount_)
        return defaultArgs[idx];
    return NULL;
}

enum argvStatus processArgs(int argc, const char *argv[]) {
    // MY_ASSERT(argv, abort());
    static bool isProcessed = false;
    if (isProcessed) {
        // logPrint(L_ZERO, 1, "Multiple argv processing is forbidden\n");
        return ARGV_ERROR;
    }
    isProcessed = true;

    flags.flags = (flagVal_t*) calloc (registeredFlagsCount_, sizeof(flagVal_t));

    if (!flags.flags) {
        // LOG_PRINT(L_DEBUG, 0, "Memory allocation failed\n");
        return ARGV_ERROR;
    }
    flags.reserved = registeredFlagsCount_;

    for (int i = 1; i < argc;) {
        if (argv[i][0] != '-')  {   //all arguments start with -
            if (defaultArgsCount_ != MAX_DEFAULT_ARGS)
                defaultArgs[defaultArgsCount_++] = argv[i];
            i++;                    //parameters of args are skipped inside scan...Argument() functions
            continue;
        }

        int remainToScan = 0;
        if (argv[i][1] == '-') //-abcd or --argument
            remainToScan = scanFullArgument(argc-i, argv+i);
        else
            remainToScan = scanShortArguments(argc-i, argv+i);

        if (remainToScan < 0) { //remainToScan < 0 is universal error code
            deleteFlags();
            // logPrint(L_ZERO, 1, "Wrong flags format\n");
            printHelpMessage();
            return ARGV_ERROR;
        }
        i  = argc - remainToScan; //moving to next arguments
    }

    char *argvConcatenated = joinStrings(argv, (size_t) argc, " ");
    //TODO: add "" on strings with " "
    // logPrint(L_DEBUG, 0, "%s\n", argvConcatenated);
    free(argvConcatenated);

    atexit(deleteFlags); //registering free function to delete flags at exit

    if (helpMessageEnabled && isFlagSet("-h"))
        return printHelpMessage();

    return ARGV_SUCCESS;
}

static int scanFullArgument(int remainToScan, const char *argv[]) {
    // MY_ASSERT(argv, abort());
    for (size_t flagIndex = 0; flagIndex < registeredFlagsCount_; flagIndex++) {        //just iterating over all flags
        if (strcmp(argv[0], flagsDescriptions[flagIndex].flagFullName) != 0) continue;
        return scanToFlag(flagsDescriptions[flagIndex], remainToScan, argv + 1) - 1;                  //we pass remainToScan forward
    }                                                                   //but scanToFlag reads flag argument, so argv+1
    return -1;                                                          //-1 because we read argv flag
}

static int scanShortArguments(int remainToScan, const char *argv[]) {
    // MY_ASSERT(argv, abort());
    for (const char *shortName = argv[0]+1; (*shortName != '\0') && (remainToScan > 0); shortName++) { //iterating over short flags string
        bool scannedArg = false;
        for (size_t flagIndex = 0; flagIndex < registeredFlagsCount_; flagIndex++) {
            if (*shortName != flagsDescriptions[flagIndex].flagShortName[1]) continue;
            scannedArg = true;

            int newRemainToScan = scanToFlag(flagsDescriptions[flagIndex], remainToScan, argv+1); //scanning flag param
            argv += remainToScan - newRemainToScan; //moving argv
            if (newRemainToScan < 0) return newRemainToScan; //checking for error
            remainToScan = newRemainToScan;
        }
        if (!scannedArg) return -1;
    }
    return remainToScan-1; //scanned current argv -> -1
}

static int scanToFlag(flagDescriptor_t desc, int remainToScan, const char *argv[]) {
    // MY_ASSERT(argv, abort());
    fVal_t val = {};

    if (desc.type != TYPE_BLANK) {
        if (--remainToScan <= 0) {
            // logPrint(L_ZERO, 1, "Expected to get parameter for flag %s, but failed\n", desc.flagFullName);
            return remainToScan;
        }
        switch(desc.type) {
        case TYPE_INT:
            sscanf(argv[0], "%d", &val.int_);
            break;
        case TYPE_FLOAT:
            sscanf(argv[0], "%lf", &val.float_);
            break;
        case TYPE_STRING:
            {
            size_t len = strlen(argv[0]);
            val.string_ = (char *) calloc(len + 1, sizeof(char));
            sscanf(argv[0], "%[^\r]", val.string_);
            break;
            }
        default:
            // MY_ASSERT(0, fprintf(stderr, "Logic error, unknown flag type"); abort(););
            break;
        }
    }

    if (addFlag(desc, val) != ARGV_SUCCESS) {
        if (desc.type == TYPE_STRING)
            FREE(val.string_);
        return -1;
    }
    return remainToScan;
}

enum argvStatus printHelpMessage() {           //building help message from flags descriptions
    if (helpMessageHeader_)
        printf("%s", helpMessageHeader_);
    printf("Available flags:\n");
    for (size_t i = 0; i < registeredFlagsCount_; i++) {
        printf("%4s, %-10s %s\n", flagsDescriptions[i].flagShortName, flagsDescriptions[i].flagFullName, flagsDescriptions[i].flagHelp);
    }
    printf("orientiered, MIPT 2024\n");
    return ARGV_HELP_MSG;
}

static flagVal_t *findFlag(const char *flagName) {
    // MY_ASSERT(flagName, abort());
    for (size_t flagIndex = 0; flagIndex < flags.size; flagIndex++) {
        if ((strcmp(flagName, flags.flags[flagIndex].desc.flagShortName) == 0) ||
            (strcmp(flagName, flags.flags[flagIndex].desc.flagFullName) == 0))
            return &(flags.flags[flagIndex]);
    }
    return NULL;
}

bool isFlagSet(const char *flagName) {
    // MY_ASSERT(flagName, abort());
    return findFlag(flagName) != NULL;
}

fVal_t getFlagValue(const char *flagName) {
    // MY_ASSERT(flagName, abort());
    flagVal_t *flag = findFlag(flagName);
    if (flag != NULL) return flag->val;
    fVal_t result = {};
    return result;
}

static enum argvStatus addFlag(flagDescriptor_t desc, fVal_t val) {
    // logPrint(L_DEBUG, 0, "Adding %s flag\n", desc.flagFullName);
    if (findFlag(desc.flagShortName) != NULL) {
        // logPrint(L_ZERO, 1, "Repeating flags not accepted\n");
        return ARGV_ERROR; //don't accept repeating flags
    }
    if (flags.size == flags.reserved) {
        // logPrint(L_ZERO, 1, "Number of flags is limited by %lu\n", flags.reserved);
        return ARGV_ERROR;
    }

    flags.flags[flags.size].desc = desc;
    flags.flags[flags.size].val = val;
    flags.size++;
    return ARGV_SUCCESS;
}

void deleteFlags() {
    for (size_t index = 0; index < flags.size; index++) {
        if (flags.flags[index].desc.type == TYPE_STRING)
            FREE(flags.flags[index].val.string_);
    }
    FREE(flags.flags);
}

static char* joinStrings(const char **strings, size_t len, const char *separator) {
    // MY_ASSERT(strings && separator, abort());

    size_t fullLen = 0;
    for (size_t idx = 0; idx < len; idx++)
        fullLen += strlen(strings[idx]);
    fullLen += strlen(separator) * (len-1);
    fullLen += 1;
    char *joined = (char*) calloc(fullLen, sizeof(char));
    char *writePtr = joined;
    for (size_t idx = 0; idx < (len - 1); idx++) {
        for (const char *strPtr = strings[idx]; *strPtr; strPtr++)
            *writePtr++ = *strPtr;
        for (const char *sepPtr = separator; *sepPtr; sepPtr++)
            *writePtr++ = *sepPtr;
    }
    for (const char *strPtr = strings[len-1]; *strPtr; strPtr++)
            *writePtr++ = *strPtr;
    *writePtr = '\0';
    return joined;
}
