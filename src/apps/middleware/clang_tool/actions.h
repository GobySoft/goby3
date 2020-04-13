#ifndef GENERATE_20190801H
#define GENERATE_20190801H

#include <string>
#include <vector>

namespace clang
{
namespace tooling
{
class ClangTool;
}
} // namespace clang

namespace goby
{
namespace clang
{
int generate(::clang::tooling::ClangTool& Tool, std::string output_directory,
             std::string output_file, std::string target_name);

struct VisualizeParameters
{
    std::string output_directory;
    std::string output_file;
    std::string deployment;
    bool omit_disconnected;
    bool include_coroner;
    bool include_terminate;
    bool include_internal;
};

int visualize(const std::vector<std::string>& ymls, const VisualizeParameters& params);
} // namespace clang
} // namespace goby

#endif
