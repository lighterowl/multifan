//  SPDX-License-Identifier: Unlicense

#include <algorithm>
#include <array>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <vector>

#include <cassert>
#include <csignal>
#include <cstdint>

#include <spdlog/cfg/env.h>
#include <spdlog/spdlog.h>

#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/ranges.h>
#include <spdlog/fmt/std.h>

#include <time.h>

namespace
{

struct source
{
    virtual ~source() = default;

    [[nodiscard]] std::optional<int32_t> last_value() const noexcept { return last_value_; }
    void update()
    {
        try
        {
            last_value_ = get();
            SPDLOG_TRACE("[{}] last_value is {}", static_cast<void *>(this), last_value_);
        }
        catch (...)
        {
            last_value_.reset();
            SPDLOG_DEBUG("[{}] source::get() threw, last_value reset'd", static_cast<void *>(this));
        }
    }

private:
    [[nodiscard]] virtual int32_t get() = 0;
    std::optional<int32_t> last_value_;
};

struct file_source : public source
{
    explicit file_source(std::filesystem::path path) : path_{std::move(path)} {}

private:
    int32_t get() override
    {
        auto f = std::ifstream{path_, std::ios::in};
        std::array<char, 32> buf;
        f.read(buf.data(), static_cast<std::streamsize>(buf.size()));

        int32_t val;
        auto [endp, ec] = std::from_chars(buf.data(), buf.data() + f.gcount(), val);
        if (ec == std::errc{})
        {
            return val;
        }

        throw std::system_error(std::make_error_code(ec));
    }
    std::filesystem::path path_;
};

struct fan
{
    using accum_fn_t = double (*)(std::span<double>);

    fan(std::filesystem::path pwm, uint8_t pwm_min, uint8_t pwm_max, accum_fn_t acc_fn)
        : pwm_(std::move(pwm)), pwm_min_(pwm_min), pwm_max_(pwm_max), accum_fn_(acc_fn)
    {
        assert(pwm_max > pwm_min);
        assert(acc_fn);
    }

    [[nodiscard]] std::string name() const noexcept { return pwm_.string(); }

    struct exception : public std::runtime_error
    {
        struct fan &fan;

    protected:
        exception(struct fan &f, const char *what) : runtime_error(what), fan(f) {}
    };

    struct reset_failed : public fan::exception
    {
        explicit reset_failed(struct fan &f) : exception(f, "Failed to set fan to manual mode") {}
    };

    void reset()
    {
        auto enable_path = pwm_;
        enable_path.replace_filename(pwm_.filename().string() + "_enable");
        {
            auto f = std::ofstream{enable_path, std::ios::out};
            if (f.is_open())
            {
                if (!(f << "1"))
                {
                    throw reset_failed(*this);
                }
            }
            else
            {
                SPDLOG_DEBUG("Could not open {}, assuming fan does not need enabling", enable_path.string());
            }
        }
        set_pwm(255);
    }

    struct set_pwm_failed : public fan::exception
    {
        set_pwm_failed(struct fan &f, uint8_t wanted_value)
            : exception(f, "Failed to set PWM value"), wanted_pwm_value(wanted_value)
        {
        }

        uint8_t const wanted_pwm_value;
    };

    void add_driver(source const &src, int32_t min, int32_t max) { drivers_.emplace_back(&src, min, max); }

    void update()
    {
        std::vector<double> values;
        values.reserve(drivers_.size());
        for (auto const &d : drivers_)
        {
            try
            {
                values.push_back(d.get_coeff());
            }
            catch (std::bad_optional_access const &e)
            {
                SPDLOG_WARN("[{}] : Source {} has no value, skipping update", static_cast<void *>(this),
                            static_cast<void const *>(d.src));
                return;
            }
        }
        SPDLOG_TRACE("Got coefficients : {}", values);
        auto const pwm_scale = accum_fn_(values);
        auto const pwm_range = pwm_max_ - pwm_min_;
        auto const final_pwm = pwm_min_ + static_cast<uint8_t>(pwm_range * pwm_scale);
        set_pwm(final_pwm);
        SPDLOG_INFO("Fan {} set to PWM {}", pwm_.string(), final_pwm);
    }

private:
    void set_pwm(uint8_t value)
    {
        auto f = std::ofstream{pwm_, std::ios::out};
        if (!(f << static_cast<int>(value)))
        {
            throw set_pwm_failed(*this, value);
        }
        SPDLOG_TRACE("PWM set to {}", value);
    }

    std::filesystem::path const pwm_;
    uint8_t const pwm_min_;
    uint8_t const pwm_max_;
    accum_fn_t const accum_fn_;

    struct driver
    {
        source const *const src;
        int32_t const min;
        int32_t const max;

        [[nodiscard]] double get_coeff() const
        {
            auto const value = src->last_value().value();
            if (value < min)
            {
                return 0.0;
            }

            if (value > max)
            {
                return 1.0;
            }

            auto const range = max - min;
            auto scaled = value - min;
            return static_cast<double>(scaled) / range;
        }
    };

    std::vector<driver> drivers_;
};

void reset_fans(std::span<fan> fans)
{
    SPDLOG_INFO("Resetting all fans to manual mode and full speed");
    for (auto &f : fans)
    {
        try
        {
            f.reset();
        }
        catch (fan::exception const &fex)
        {
            SPDLOG_CRITICAL("Failed to reset fan {}, quitting", fex.fan.name());
            throw;
        }
    }
}

template <class Rep, class Period> void interruptible_sleep(std::chrono::duration<Rep, Period> duration)
{
    auto const ts =
        timespec{std::chrono::seconds{duration}.count(), std::chrono::nanoseconds{duration}.count() % std::nano::den};
    ::nanosleep(&ts, nullptr);
}

std::atomic_bool g_signal_caught = false;

void on_signal(int) { g_signal_caught = true; }

void main_loop(std::span<source *> sources, std::span<fan> fans)
{
    constexpr auto poll_interval = std::chrono::seconds(5);
    while (!g_signal_caught)
    {
        for (auto s : sources)
        {
            s->update();
        }
        for (auto &f : fans)
        {
            f.update();
        }

        interruptible_sleep(poll_interval);
    }
}
} // namespace

int main()
{
    spdlog::cfg::load_env_levels();
    (void)std::signal(SIGTERM, &on_signal);
    (void)std::signal(SIGINT, &on_signal);

    auto cpu_source = file_source{"/sys/devices/platform/coretemp.0/hwmon/hwmon1/temp1_input"};
    auto hdd_source =
        file_source{"/sys/devices/pci0000:00/0000:00:17.0/ata1/host0/target0:0:0/0:0:0:0/hwmon/hwmon3/temp1_input"};

    auto fans = std::array<fan, 1>{{{"/sys/devices/platform/nct6775.672/hwmon/hwmon2/pwm2", 60, 240,
                                     [](auto vals) { return *std::max_element(vals.begin(), vals.end()); }}}};

    fans[0].add_driver(cpu_source, 60000, 80000);
    fans[0].add_driver(hdd_source, 42000, 50000);

    auto sources = std::array<source *, 2>{&cpu_source, &hdd_source};

    int rv = 0;
    try
    {
        reset_fans(fans);
        main_loop(sources, fans);
        SPDLOG_INFO("Signal caught, program will now exit");
    }
    catch (std::exception const &ex)
    {
        SPDLOG_WARN("Exception caught : {}, program will now exit", ex.what());
        rv = 1;
    }

    reset_fans(fans);
    return rv;
}
