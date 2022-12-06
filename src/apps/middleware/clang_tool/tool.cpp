// Copyright 2020-2022:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//
//
// This file is part of the Goby Underwater Autonomy Project Binaries
// ("The Goby Binaries").
//
// The Goby Binaries are free software: you can redistribute them and/or modify
// them under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// The Goby Binaries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#include <iostream> // for operator<<, endl, bas...
#include <stdlib.h> // for exit, EXIT_FAILURE
#include <string>   // for string

#include <llvm/ADT/ArrayRef.h>  // for ArrayRef
#include <llvm/ADT/StringRef.h> // for StringRef

#include "actions.h"                           // for generate, visualize
#include "clang/Tooling/CommonOptionsParser.h" // for CommonOptionsParser
#include "clang/Tooling/Tooling.h"             // for ClangTool
#include "llvm/Support/CommandLine.h"          // for opt, cat, desc, value...

namespace cl = llvm::cl;

// Apply a custom category to all command-line options so that they are the
// only ones displayed.
static cl::OptionCategory Goby3ToolCategory("goby_clang_tool options");

// CommonOptionsParser declares HelpMessage with a description of the common
// command-line options related to the compilation database and input files.
// It's nice to have this help message in all tools.
static cl::extrahelp CommonHelp(clang::tooling::CommonOptionsParser::HelpMessage);

static cl::opt<bool>
    Generate("gen",
             cl::desc("Run generate action (create YML interface files from C++ source code)"),
             cl::cat(Goby3ToolCategory));
static cl::opt<bool> Visualize(
    "viz",
    cl::desc("Run visualize action (create GraphViz DOT files from multiple YML interface files)"),
    cl::cat(Goby3ToolCategory));

static cl::opt<std::string>
    OutDir("outdir", cl::desc("Specify output directory for '-viz' and '-gen' actions"),
           cl::value_desc("dir"), cl::init("."), cl::cat(Goby3ToolCategory));

static cl::opt<std::string>
    OutFile("o",
            cl::desc("Specify output file name (optional, defaults to {target}_interface.yml for "
                     "-gen and {deployment}.dot for -viz)"),
            cl::value_desc("file.[yml|dot]"), cl::cat(Goby3ToolCategory));

static cl::opt<std::string> Target("target",
                                   cl::desc("Specify target (binary) name for '-gen' action"),
                                   cl::value_desc("name"), cl::cat(Goby3ToolCategory));

static cl::opt<std::string>
    Deployment("deployment",
               cl::desc("Specify deployment name for '-viz' action that summarizes the collection "
                        "of yml files or the path to a deployment yml file"),
               cl::value_desc("name"), cl::cat(Goby3ToolCategory));

static cl::opt<bool> OmitDisconnected(
    "no-disconnected",
    cl::desc("For '-viz', do not display arrows representing publishers without subscribers "
             "or subscribers without publishers"),
    cl::cat(Goby3ToolCategory));

static cl::opt<std::string> OmitGroupRegex("omit-group-regex",
                                           cl::desc("Regex of groups to omit for '-viz' action"),
                                           cl::value_desc("foo.*"), cl::cat(Goby3ToolCategory));

static cl::opt<std::string> OmitNodeRegex("omit-node-regex",
                                          cl::desc("Regex of nodes to omit for '-viz' action"),
                                          cl::value_desc("foo.*"), cl::cat(Goby3ToolCategory));

static cl::opt<bool> IncludeTerminate("include-terminate",
                                      cl::desc("For '-viz', include goby_terminate groups"),
                                      cl::cat(Goby3ToolCategory));

static cl::opt<bool> IncludeCoroner("include-coroner",
                                    cl::desc("For '-viz', include goby_coroner groups"),
                                    cl::cat(Goby3ToolCategory));

static cl::opt<std::string> DotSplines("splines", cl::desc("For '-viz', Graphviz spline= setting"),
                                       cl::value_desc("ortho"), cl::init("ortho"),
                                       cl::cat(Goby3ToolCategory));

static cl::opt<bool>
    IncludeAll("include-all",
               cl::desc("For '-viz', include all groups, include goby internal groups"),
               cl::cat(Goby3ToolCategory));

int main(int argc, const char** argv)
{
    llvm::Expected<clang::tooling::CommonOptionsParser> SharedOptionsParser =
        clang::tooling::CommonOptionsParser::create(argc, argv, Goby3ToolCategory, llvm::cl::OneOrMore);

    if (Generate)
    {
        if (Target.empty())
        {
            std::cerr << "Must specify -target when using -gen" << std::endl;
            exit(EXIT_FAILURE);
        }
        clang::tooling::ClangTool Tool(SharedOptionsParser->getCompilations(),
                                       SharedOptionsParser->getSourcePathList());
        return goby::clang::generate(Tool, OutDir, OutFile, Target);
    }
    else if (Visualize)
    {
        goby::clang::VisualizeParameters params{OutDir,
                                                OutFile,
                                                Deployment,
                                                OmitDisconnected,
                                                IncludeAll ? true : IncludeCoroner,
                                                IncludeAll ? true : IncludeTerminate,
                                                IncludeAll,
                                                DotSplines,
                                                OmitGroupRegex,
                                                OmitNodeRegex};

        return goby::clang::visualize(SharedOptionsParser->getSourcePathList(), params);
    }
    else
    {
        std::cerr << "Must specify an action (e.g. -gen or -viz)" << std::endl;
        exit(EXIT_FAILURE);
    }
}
