#pragma once

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#ifndef BLUE_VERSION
#define BLUE_VERSION "dev"
#endif

struct CliOptions {
    bool doCompile = false;
    bool printC = false;
    bool doInit = false;
    bool doBuild = false;
    bool doRun = false;
    bool doBenchmark = false;
    bool initThenBuild = false;
    bool noInlineHtml = false;
    std::string projDirInit;
    std::string projDirBuild;
    std::string inputPath;
    std::string outputBinary;
    std::string extraCFlags;
    std::vector<std::string> appArgs;
};

enum class CliParseResult {
    Ok,
    ExitSuccess,
    ExitFailure,
};

static CliParseResult parseArgs(int argc, char** argv, CliOptions& opts,
                                void (*usage)(const char*)) {
    if (argc < 2) {
        usage(argv[0]);
        return CliParseResult::ExitFailure;
    }

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--version" || a == "-version") {
            std::cout << "blue " << BLUE_VERSION << "\n";
            return CliParseResult::ExitSuccess;
        }
        if (a == "-h" || a == "--help") {
            usage(argv[0]);
            return CliParseResult::ExitSuccess;
        }
        if (a == "-compile")
            opts.doCompile = true;
        else if (a == "--print-c")
            opts.printC = true;
        else if (a == "-init" || (a == "init" && !opts.doBuild)) {
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                opts.projDirInit = argv[++i];
                opts.doInit = true;
            } else {
                opts.projDirInit = ".";
                opts.doInit = true;
            }
        }
        else if (a == "-build" || a == "build" || a == "run" ||
                 (a == "init" && opts.doBuild)) {
            if (a == "run")
                opts.doRun = true;
            if (i + 1 < argc && std::string(argv[i + 1]) == "init") {
                if (i + 2 >= argc) {
                    std::cerr << "blue: " << a << " init requires <dir>\n";
                    return CliParseResult::ExitFailure;
                }
                opts.initThenBuild = true;
                opts.projDirBuild = argv[i + 2];
                i += 2;
            } else if (i + 1 < argc && argv[i + 1][0] != '-') {
                opts.projDirBuild = argv[++i];
            } else {
                opts.projDirBuild = ".";
            }
            opts.doBuild = true;
        }
        else if (a == "--time" || a == "--benchmark")
            opts.doBenchmark = true;
        else if (a == "--") {
            for (int j = i + 1; j < argc; ++j)
                opts.appArgs.push_back(argv[j]);
            break;
        }
        else if (a == "--no-inline-html")
            opts.noInlineHtml = true;
        else if (a == "-o" || a == "--output") {
            if (i + 1 >= argc) {
                std::cerr << "blue: -o requires a filename argument\n";
                return CliParseResult::ExitFailure;
            }
            opts.outputBinary = argv[++i];
        }
        else if (a == "-cflags") {
            if (i + 1 >= argc) {
                std::cerr << "blue: -cflags requires a flags argument\n";
                return CliParseResult::ExitFailure;
            }
            opts.extraCFlags = argv[++i];
        }
        else if (a[0] != '-')
            opts.inputPath = a;
        else {
            std::cerr << "blue: unknown flag: " << a << "\n";
            usage(argv[0]);
            return CliParseResult::ExitFailure;
        }
    }

    return CliParseResult::Ok;
}
