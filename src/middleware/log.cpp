#include "log.h"

boost::bimap<std::string, goby::uint<goby::LogEntry::group_bytes_>::type> goby::LogEntry::groups_;
boost::bimap<std::string, goby::uint<goby::LogEntry::type_bytes_>::type> goby::LogEntry::types_;

goby::uint<goby::LogEntry::group_bytes_>::type goby::LogEntry::group_index_(1);
goby::uint<goby::LogEntry::type_bytes_>::type goby::LogEntry::type_index_(1);
