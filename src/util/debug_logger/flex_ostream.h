// Copyright 2012-2020:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//
//
// This file is part of the Goby Underwater Autonomy Project Libraries
// ("The Goby Libraries").
//
// The Goby Libraries are free software: you can redistribute them and/or modify
// them under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 2.1 of the License, or
// (at your option) any later version.
//
// The Goby Libraries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#ifndef FlexOstream20091211H
#define FlexOstream20091211H

#include <iomanip>
#include <iostream>
#include <string>

#include <google/protobuf/io/tokenizer.h>
#include <google/protobuf/text_format.h>

#include "goby/util/debug_logger/flex_ostreambuf.h"
#include "logger_manipulators.h"

namespace goby
{
namespace util
{
/// Forms the basis of the Goby logger: std::ostream derived class for holding the FlexOStreamBuf
class FlexOstream : public std::ostream
{
  public:
    FlexOstream() : std::ostream(&sb_), sb_(this), lock_action_(logger_lock::none)
    {
        ++instances_;
        if (instances_ > 1)
        {
            std::cerr << "Fatal error: cannot create more than one instance of FlexOstream. Use "
                         "global object goby::glog to access the Goby Logger. Do not instantiate "
                         "the FlexOstream directly."
                      << std::endl;
            exit(EXIT_FAILURE);
        }
    }
    ~FlexOstream() {}

    /// \name Initialization
    //@{
    /// Add another group to the logger. A group provides related manipulator for categorizing log messages.
    void add_group(const std::string& name, Colors::Color color = Colors::nocolor,
                   const std::string& description = "");

    /// Set the name of the application that the logger is serving.
    void set_name(const std::string& s)
    {
        std::lock_guard<std::recursive_mutex> l(goby::util::logger::mutex);
        sb_.name(s);
    }

    void enable_gui()
    {
        std::lock_guard<std::recursive_mutex> l(goby::util::logger::mutex);
        sb_.enable_gui();
    }

    bool is(goby::util::logger::Verbosity verbosity);

    bool is_die() { return is(goby::util::logger::DIE); }
    bool is_warn() { return is(goby::util::logger::WARN); }
    bool is_verbose() { return is(goby::util::logger::VERBOSE); }
    bool is_debug1() { return is(goby::util::logger::DEBUG1); }
    bool is_debug2() { return is(goby::util::logger::DEBUG2); }
    bool is_debug3() { return is(goby::util::logger::DEBUG3); }

    /// Attach a stream object (e.g. std::cout, std::ofstream, ...) to the logger with desired verbosity
    void add_stream(logger::Verbosity verbosity = logger::VERBOSE, std::ostream* os = 0)
    {
        std::lock_guard<std::recursive_mutex> l(goby::util::logger::mutex);
        sb_.add_stream(verbosity, os);
    }

    void add_stream(goby::util::protobuf::GLogConfig::Verbosity verbosity =
                        goby::util::protobuf::GLogConfig::VERBOSE,
                    std::ostream* os = 0)
    {
        std::lock_guard<std::recursive_mutex> l(goby::util::logger::mutex);
        sb_.add_stream(static_cast<logger::Verbosity>(verbosity), os);
    }

    const FlexOStreamBuf& buf() { return sb_; }

    //@}

    /// \name Overloaded insert stream operator<<
    //@{
    // overload this function so we can look for the die manipulator
    // and set the die_flag
    // to exit after this line
    std::ostream& operator<<(FlexOstream& (*pf)(FlexOstream&));
    std::ostream& operator<<(std::ostream& (*pf)(std::ostream&));

    //provide interfaces to the rest of the types
    std::ostream& operator<<(bool& val) { return std::ostream::operator<<(val); }
    std::ostream& operator<<(const short& val) { return std::ostream::operator<<(val); }
    std::ostream& operator<<(const unsigned short& val) { return std::ostream::operator<<(val); }
    std::ostream& operator<<(const int& val) { return std::ostream::operator<<(val); }
    std::ostream& operator<<(const unsigned int& val) { return std::ostream::operator<<(val); }
    std::ostream& operator<<(const long& val) { return std::ostream::operator<<(val); }
    std::ostream& operator<<(const long long& val) { return std::ostream::operator<<(val); }
    std::ostream& operator<<(const unsigned long& val) { return std::ostream::operator<<(val); }
    std::ostream& operator<<(const float& val) { return std::ostream::operator<<(val); }
    std::ostream& operator<<(const double& val) { return std::ostream::operator<<(val); }
    std::ostream& operator<<(const long double& val) { return std::ostream::operator<<(val); }
    std::ostream& operator<<(std::streambuf* sb) { return std::ostream::operator<<(sb); }
    std::ostream& operator<<(std::ios& (*pf)(std::ios&)) { return std::ostream::operator<<(pf); }
    std::ostream& operator<<(std::ios_base& (*pf)(std::ios_base&))
    {
        return std::ostream::operator<<(pf);
    }

    //@}

    /// \name Thread safety related
    //@{
    /// Get a reference to the Goby logger mutex for scoped locking
    std::recursive_mutex& mutex() { return logger::mutex; }

    void set_lock_action(logger_lock::LockAction lock_action) { sb_.set_lock_action(lock_action); }
    //@}

    void refresh() { sb_.refresh(); }
    void set_group(const std::string& s) { sb_.group_name(s); }

    void set_unset_verbosity()
    {
        if (sb_.verbosity_depth() == goby::util::logger::UNKNOWN)
            this->is(goby::util::logger::VERBOSE);
    }

  private:
    FlexOstream(const FlexOstream&);
    FlexOstream& operator=(const FlexOstream&);

    bool quiet() { return (sb_.is_quiet()); }

    friend std::ostream& operator<<(FlexOstream& out, char c);
    friend std::ostream& operator<<(FlexOstream& out, signed char c);
    friend std::ostream& operator<<(FlexOstream& out, unsigned char c);
    friend std::ostream& operator<<(FlexOstream& out, const char* s);
    friend std::ostream& operator<<(FlexOstream& out, const signed char* s);
    friend std::ostream& operator<<(FlexOstream& out, const unsigned char* s);

  private:
    static int instances_;

  private:
    FlexOStreamBuf sb_;
    logger_lock::LockAction lock_action_;
};

inline std::ostream& operator<<(FlexOstream& out, char c) { return std::operator<<(out, c); }
inline std::ostream& operator<<(FlexOstream& out, signed char c) { return std::operator<<(out, c); }
inline std::ostream& operator<<(FlexOstream& out, unsigned char c)
{
    return std::operator<<(out, c);
}
inline std::ostream& operator<<(FlexOstream& out, const char* s) { return std::operator<<(out, s); }
inline std::ostream& operator<<(FlexOstream& out, const signed char* s)
{
    return std::operator<<(out, s);
}
inline std::ostream& operator<<(FlexOstream& out, const unsigned char* s)
{
    return std::operator<<(out, s);
}
//@}

template <typename _CharT, typename _Traits, typename _Alloc>
inline std::ostream& operator<<(FlexOstream& out,
                                const std::basic_string<_CharT, _Traits, _Alloc>& s)
{
    return std::operator<<(out, s);
}

} // namespace util

/// \name Logger
//@{
/// \brief Access the Goby logger through this object.
extern util::FlexOstream glog;
//@}
namespace util
{
// for compatibility with Goby1
[[deprecated("Use goby::glog")]] inline util::FlexOstream& glogger() { return goby::glog; }

class FlexOStreamErrorCollector : public google::protobuf::io::ErrorCollector
{
  public:
    FlexOStreamErrorCollector(const std::string& original)
        : original_(original), has_warnings_(false), has_errors_(false)
    {
    }

    void AddError(int line, int column, const std::string& message)
    {
        using goby::util::logger::WARN;

        print_original(line, column);
        goby::glog.is(WARN) && goby::glog << "line: " << line << " col: " << column << " "
                                          << message << std::endl;
        has_errors_ = true;
    }
    void AddWarning(int line, int column, const std::string& message)
    {
        using goby::util::logger::WARN;

        print_original(line, column);
        goby::glog.is(WARN) && goby::glog << "line: " << line << " col: " << column << " "
                                          << message << std::endl;

        has_warnings_ = true;
    }

    void print_original(int line, int column)
    {
        using goby::util::logger::WARN;

        std::stringstream ss(original_ + "\n");
        std::string line_str;

        //for(int i = 0; i <= line; ++i)
        //    getline(ss, line_str);

        int i = 0;
        while (!getline(ss, line_str).eof())
        {
            if (i == line)
                goby::glog.is(WARN) && goby::glog << goby::util::tcolor::lt_red << "[line "
                                                  << std::setw(3) << i++ << "]" << line_str
                                                  << goby::util::tcolor::nocolor << std::endl;
            else
                goby::glog.is(WARN) && goby::glog << "[line " << std::setw(3) << i++ << "]"
                                                  << line_str << std::endl;
        }
    }

    bool has_errors() { return has_errors_; }
    bool has_warnings() { return has_warnings_; }

  private:
    const std::string& original_;
    bool has_warnings_;
    bool has_errors_;
};
} // namespace util
} // namespace goby

#endif
