// Copyright 2026:
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

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/prctl.h>
#endif

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>

#include <boost/units/systems/si.hpp>

#include "fixture.h"

extern char** environ;

using goby::middleware::protobuf::PubSubTestFixtureReport;
using Clock = std::chrono::steady_clock;

namespace
{
constexpr std::size_t tail_lines{50};
constexpr std::size_t report_tail_lines{20};

std::mutex reap_mutex;
std::map<pid_t, int> exit_statuses;

// process groups still running, so they can be killed if the tool exits abnormally
std::mutex live_mutex;
std::set<pid_t> live_groups;

void emergency_cleanup()
{
    std::set<pid_t> groups;
    {
        std::lock_guard<std::mutex> lock(live_mutex);
        groups.swap(live_groups);
    }
    for (auto g : groups) ::kill(-g, SIGTERM);
    if (!groups.empty())
        ::usleep(500000);
    for (auto g : groups) ::kill(-g, SIGKILL);
    goby::apps::middleware::kill_descendants();
}

void register_group(pid_t g)
{
    static bool registered = false;
    std::lock_guard<std::mutex> lock(live_mutex);
    if (!registered)
    {
        std::atexit(emergency_cleanup);
        registered = true;
    }
    live_groups.insert(g);
}

void unregister_group(pid_t g)
{
    std::lock_guard<std::mutex> lock(live_mutex);
    live_groups.erase(g);
}

bool group_alive(pid_t g)
{
    goby::apps::middleware::reap_children();
    return ::kill(-g, 0) == 0 || errno == EPERM;
}

bool wait_for(const std::function<bool()>& done, double seconds)
{
    const auto deadline = Clock::now() + std::chrono::duration_cast<Clock::duration>(
                                             std::chrono::duration<double>(seconds));
    while (!done())
    {
        if (Clock::now() >= deadline)
            return false;
        ::usleep(50000);
    }
    return true;
}

std::string number(double d)
{
    std::ostringstream ss;
    ss << d;
    return ss.str();
}

double seconds_since(Clock::time_point t)
{
    return std::chrono::duration<double>(Clock::now() - t).count();
}

#ifdef __linux__
// living (non-zombie) descendants of this process, from /proc
std::vector<pid_t> living_descendants()
{
    std::map<pid_t, std::vector<pid_t>> children;
    std::set<pid_t> zombies;
    if (DIR* proc = ::opendir("/proc"))
    {
        while (dirent* entry = ::readdir(proc))
        {
            char* end = nullptr;
            const long pid = std::strtol(entry->d_name, &end, 10);
            if (!end || *end != '\0' || pid <= 0)
                continue;
            std::ifstream stat("/proc/" + std::string(entry->d_name) + "/stat");
            std::string line;
            if (!std::getline(stat, line))
                continue;
            // comm may contain spaces and parentheses; fields resume after the last ')'
            auto paren = line.rfind(')');
            if (paren == std::string::npos || paren + 4 > line.size())
                continue;
            char state = line[paren + 2];
            const long ppid = std::strtol(line.c_str() + paren + 4, nullptr, 10);
            children[ppid].push_back(pid);
            if (state == 'Z')
                zombies.insert(pid);
        }
        ::closedir(proc);
    }

    std::vector<pid_t> out, frontier{::getpid()};
    while (!frontier.empty())
    {
        pid_t p = frontier.back();
        frontier.pop_back();
        for (auto c : children[p])
        {
            frontier.push_back(c);
            if (!zombies.count(c))
                out.push_back(c);
        }
    }
    return out;
}
#endif
} // namespace

int goby::apps::middleware::signal_from_name(const std::string& name)
{
    static const std::map<std::string, int> signals{
        {"INT", SIGINT},   {"TERM", SIGTERM}, {"HUP", SIGHUP},   {"QUIT", SIGQUIT},
        {"KILL", SIGKILL}, {"USR1", SIGUSR1}, {"USR2", SIGUSR2}, {"ABRT", SIGABRT}};
    std::string n = name;
    if (n.rfind("SIG", 0) == 0)
        n = n.substr(3);
    auto it = signals.find(n);
    if (it != signals.end())
        return it->second;
    char* end = nullptr;
    long num = std::strtol(name.c_str(), &end, 10);
    return (end && *end == '\0' && num > 0 && num < NSIG) ? static_cast<int>(num) : 0;
}

void goby::apps::middleware::reap_children()
{
    std::lock_guard<std::mutex> lock(reap_mutex);
    int status = 0;
    pid_t p;
    while ((p = ::waitpid(-1, &status, WNOHANG)) > 0) exit_statuses[p] = status;
}

void goby::apps::middleware::enable_child_subreaper()
{
#ifdef __linux__
    ::prctl(PR_SET_CHILD_SUBREAPER, 1);
#endif
}

void goby::apps::middleware::kill_descendants()
{
#ifdef __linux__
    auto remaining = living_descendants();
    if (remaining.empty())
        return;
    for (auto p : remaining) ::kill(p, SIGTERM);
    wait_for(
        []
        {
            reap_children();
            return living_descendants().empty();
        },
        2.0);
    for (auto p : living_descendants()) ::kill(p, SIGKILL);
    wait_for(
        []
        {
            reap_children();
            return living_descendants().empty();
        },
        2.0);
    reap_children();
#endif
}

goby::apps::middleware::Fixture::Fixture(const protobuf::TestFixture& cfg, std::string log_path)
    : cfg_(cfg), log_path_(std::move(log_path))
{
    if (cfg_.ready().has_output_regex())
        ready_regex_ = std::make_unique<std::regex>(cfg_.ready().output_regex());
}

goby::apps::middleware::Fixture::~Fixture()
{
    if (pid_ > 0)
        stop();
    close_output();
}

void goby::apps::middleware::Fixture::start()
{
    int fds[2];
    if (::pipe(fds) != 0)
        throw std::runtime_error(std::string("pipe: ") + std::strerror(errno));
    // keep later fixtures from inheriting this pipe
    ::fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    ::fcntl(fds[1], F_SETFD, FD_CLOEXEC);

    // everything the child needs is prepared before fork(), which only allows async-signal-safe calls
    std::vector<std::string> env_strings;
    std::set<std::string> overridden;
    for (const auto& e : cfg_.env()) overridden.insert(e.substr(0, e.find('=')));
    for (char** e = environ; *e; ++e)
    {
        std::string entry(*e);
        if (!overridden.count(entry.substr(0, entry.find('='))))
            env_strings.push_back(entry);
    }
    for (const auto& e : cfg_.env()) env_strings.push_back(e);
    std::vector<char*> envp;
    for (auto& e : env_strings) envp.push_back(&e[0]);
    envp.push_back(nullptr);

    std::string sh = "/bin/sh", dash_c = "-c", command = cfg_.command(), cwd = cfg_.cwd();
    char* argv[] = {&sh[0], &dash_c[0], &command[0], nullptr};

    pid_ = ::fork();
    if (pid_ < 0)
        throw std::runtime_error(std::string("fork: ") + std::strerror(errno));

    if (pid_ == 0)
    {
        ::setpgid(0, 0);
        ::dup2(fds[1], STDOUT_FILENO);
        ::dup2(fds[1], STDERR_FILENO);
        int devnull = ::open("/dev/null", O_RDONLY);
        if (devnull >= 0)
            ::dup2(devnull, STDIN_FILENO);
        if (!cwd.empty() && ::chdir(cwd.c_str()) != 0)
        {
            const char msg[] = "goby test: cannot change to the fixture's working directory\n";
            ssize_t ignored = ::write(STDERR_FILENO, msg, sizeof(msg) - 1);
            (void)ignored;
            ::_exit(127);
        }
        ::execve(sh.c_str(), argv, envp.data());
        ::_exit(127);
    }

    // set in both processes so the group exists before either side relies on it
    ::setpgid(pid_, pid_);
    ::close(fds[1]);
    out_fd_ = fds[0];
    register_group(pid_);
    reader_ = std::thread([this]() { read_output(); });
}

void goby::apps::middleware::Fixture::read_output()
{
    std::ofstream log(log_path_);
    std::string partial;
    char buf[4096];

    auto add_line = [&](std::string line)
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::lock_guard<std::mutex> lock(mutex_);
        tail_.push_back(line);
        if (tail_.size() > tail_lines)
            tail_.pop_front();
        if (ready_regex_ && !matched_ && std::regex_search(line, *ready_regex_))
        {
            matched_ = true;
            cv_.notify_all();
        }
    };

    for (;;)
    {
        pollfd pfd{out_fd_, POLLIN, 0};
        int r = ::poll(&pfd, 1, 100);
        if (r < 0 && errno == EINTR)
            continue;
        if (r == 0)
        {
            if (stop_reading_)
                break;
            continue;
        }
        ssize_t n = ::read(out_fd_, buf, sizeof(buf));
        if (n < 0 && (errno == EINTR || errno == EAGAIN))
            continue;
        if (n <= 0)
            break;
        log.write(buf, n);
        log.flush();
        partial.append(buf, n);
        std::string::size_type pos;
        while ((pos = partial.find('\n')) != std::string::npos)
        {
            add_line(partial.substr(0, pos));
            partial.erase(0, pos + 1);
        }
    }
    if (!partial.empty())
        add_line(partial);
}

void goby::apps::middleware::Fixture::update_exit_status()
{
    if (exited_ || pid_ <= 0)
        return;
    reap_children();
    std::lock_guard<std::mutex> lock(reap_mutex);
    auto it = exit_statuses.find(pid_);
    if (it == exit_statuses.end())
        return;
    exited_ = true;
    if (WIFEXITED(it->second))
        exit_status_ = WEXITSTATUS(it->second);
    else if (WIFSIGNALED(it->second))
        exit_signal_ = WTERMSIG(it->second);
}

bool goby::apps::middleware::Fixture::wait_ready(const std::function<bool()>& cancelled)
{
    const auto start = Clock::now();

    auto exited_reason = [this]()
    {
        return exit_signal_
                   ? "exited on signal " + std::to_string(exit_signal_) + " before ready"
                   : "exited with status " + std::to_string(exit_status_) + " before ready";
    };

    if (ready_regex_)
    {
        const auto deadline = start + std::chrono::duration_cast<Clock::duration>(
                                          std::chrono::duration<double>(cfg_.ready().timeout()));
        for (;;)
        {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (cv_.wait_for(lock, std::chrono::milliseconds(50), [this] { return matched_; }))
                    break;
            }
            if (cancelled())
            {
                reason_ = "interrupted before ready";
                return false;
            }
            update_exit_status();
            if (exited_)
            {
                // the line may have arrived just before exit
                std::lock_guard<std::mutex> lock(mutex_);
                if (matched_)
                    break;
                reason_ = exited_reason();
                return false;
            }
            if (Clock::now() > deadline)
            {
                reason_ = "no output matched /" + cfg_.ready().output_regex() + "/ within " +
                          number(cfg_.ready().timeout()) + " s";
                return false;
            }
        }
    }

    const auto delay_end = Clock::now() + std::chrono::duration_cast<Clock::duration>(
                                              std::chrono::duration<double>(cfg_.ready().delay()));
    while (Clock::now() < delay_end)
    {
        if (cancelled())
        {
            reason_ = "interrupted before ready";
            return false;
        }
        ::usleep(20000);
    }

    ready_ = true;
    ready_time_ = seconds_since(start);
    return true;
}

void goby::apps::middleware::Fixture::stop()
{
    if (pid_ <= 0)
        return;
    update_exit_status();
    stopped_by_us_ = !exited_;

    const pid_t group = pid_;
    if (group_alive(group))
    {
        ::kill(-group, signal_from_name(cfg_.stop().signal()));
        if (!wait_for([group] { return !group_alive(group); }, cfg_.stop().grace()))
        {
            ::kill(-group, SIGTERM);
            if (!wait_for([group] { return !group_alive(group); }, 2.0))
            {
                ::kill(-group, SIGKILL);
                wait_for([group] { return !group_alive(group); }, 2.0);
            }
        }
    }
    wait_for(
        [this]
        {
            update_exit_status();
            return exited_;
        },
        2.0);
    unregister_group(group);
    pid_ = -pid_; // remembered for reporting, never signalled again
}

void goby::apps::middleware::Fixture::close_output()
{
    stop_reading_ = true;
    if (reader_.joinable())
        reader_.join();
    if (out_fd_ >= 0)
    {
        ::close(out_fd_);
        out_fd_ = -1;
    }
}

void goby::apps::middleware::Fixture::fill_report(PubSubTestFixtureReport& report,
                                                  bool include_tail) const
{
    using goby::middleware::protobuf::PubSubTestResult;
    report.set_name(cfg_.name());
    report.set_log_file(log_path_);

    if (!ready_)
    {
        report.set_result(goby::middleware::protobuf::ERROR);
        report.set_reason(reason_.empty() ? "not ready" : reason_);
    }
    else
    {
        report.set_ready_time_with_units(ready_time_ * boost::units::si::seconds);
        const bool exited_on_its_own = exited_ && !stopped_by_us_;
        if (exited_on_its_own && exit_status_ == 0 && exit_signal_ == 0)
        {
            report.set_result(goby::middleware::protobuf::PASS);
            report.set_reason("exited with status 0 before the test ended");
        }
        else if (exited_on_its_own)
        {
            report.set_result(goby::middleware::protobuf::FAIL);
            report.set_reason(exit_signal_ ? "ended by signal " + std::to_string(exit_signal_) +
                                                 " before the test ended"
                                           : "exited with status " + std::to_string(exit_status_) +
                                                 " before the test ended");
        }
        else
        {
            report.set_result(goby::middleware::protobuf::PASS);
        }
    }

    if (exited_ && !stopped_by_us_)
    {
        if (exit_signal_)
            report.set_exit_signal(exit_signal_);
        else
            report.set_exit_status(exit_status_);
    }

    if (include_tail)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto first =
            tail_.size() > report_tail_lines ? tail_.end() - report_tail_lines : tail_.begin();
        for (auto it = first; it != tail_.end(); ++it) report.add_output_tail(*it);
    }
}

goby::apps::middleware::FixtureSet::FixtureSet(
    const google::protobuf::RepeatedPtrField<protobuf::TestFixture>& cfgs, std::string log_dir)
    : log_dir_(std::move(log_dir))
{
    if (cfgs.empty())
        return;

    if (log_dir_.empty())
    {
        const char* tmp = std::getenv("TMPDIR");
        std::string templ = std::string(tmp && *tmp ? tmp : "/tmp") + "/goby_test_XXXXXX";
        if (!::mkdtemp(&templ[0]))
            throw std::runtime_error("cannot create a log directory: " +
                                     std::string(std::strerror(errno)));
        log_dir_ = templ;
    }
    else
    {
        std::filesystem::create_directories(log_dir_);
    }

    for (const auto& cfg : cfgs)
    {
        std::string file = cfg.name();
        for (auto& c : file)
            if (c == '/' || c == ' ')
                c = '_';
        fixtures_.push_back(std::make_unique<Fixture>(cfg, log_dir_ + "/" + file + ".log"));
    }
}

goby::apps::middleware::FixtureSet::~FixtureSet() { stop_all(); }

bool goby::apps::middleware::FixtureSet::start_all(const std::function<bool()>& cancelled)
{
    for (auto& f : fixtures_)
    {
        f->start();
        if (!f->wait_ready(cancelled))
            return false;
    }
    return true;
}

void goby::apps::middleware::FixtureSet::stop_all()
{
    if (stopped_)
        return;
    stopped_ = true;
    for (auto it = fixtures_.rbegin(); it != fixtures_.rend(); ++it) (*it)->stop();
    kill_descendants();
    for (auto& f : fixtures_) f->close_output();
}

void goby::apps::middleware::FixtureSet::fill_reports(
    google::protobuf::RepeatedPtrField<PubSubTestFixtureReport>& reports, bool include_tail) const
{
    for (const auto& f : fixtures_)
        if (f->started())
            f->fill_report(*reports.Add(), include_tail);
}
