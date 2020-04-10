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
int visualize(const std::vector<std::string>& ymls, std::string output_directory,
              std::string output_file, std::string deployment_name, bool omit_disconnected);
} // namespace clang
} // namespace goby

#endif
