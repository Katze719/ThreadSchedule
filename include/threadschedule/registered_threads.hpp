#pragma once

#include "pthread_wrapper.hpp"
#include "thread_registry.hpp"
#include "thread_wrapper.hpp"
#include <utility>

namespace threadschedule
{

// Registered std::thread wrapper (opt-in)
class ThreadWrapperReg : public ThreadWrapper
{
  public:
    ThreadWrapperReg() = default;

    template <typename F, typename... Args>
    explicit ThreadWrapperReg(std::string name, std::string componentTag, F &&f, Args &&...args)
        : ThreadWrapper(
              [n = std::move(name), c = std::move(componentTag), func = std::forward<F>(f)](Args... inner) {
                  AutoRegisterCurrentThread guard(n, c);
                  func(std::forward<Args>(inner)...);
              },
              std::forward<Args>(args)...)
    {
    }

    template <typename F, typename... Args>
    explicit ThreadWrapperReg(F &&f, Args &&...args)
        : ThreadWrapper(
              [func = std::forward<F>(f)](Args... inner) {
                  AutoRegisterCurrentThread guard;
                  func(std::forward<Args>(inner)...);
              },
              std::forward<Args>(args)...)
    {
    }
};

#if __cplusplus >= 202002L
class JThreadWrapperReg : public JThreadWrapper
{
  public:
    JThreadWrapperReg() = default;

    template <typename F, typename... Args>
    explicit JThreadWrapperReg(std::string name, std::string componentTag, F &&f, Args &&...args)
        : JThreadWrapper(
              [n = std::move(name), c = std::move(componentTag), func = std::forward<F>(f)](Args... inner) {
                  AutoRegisterCurrentThread guard(n, c);
                  func(std::forward<Args>(inner)...);
              },
              std::forward<Args>(args)...)
    {
    }

    template <typename F, typename... Args>
    explicit JThreadWrapperReg(F &&f, Args &&...args)
        : JThreadWrapper(
              [func = std::forward<F>(f)](Args... inner) {
                  AutoRegisterCurrentThread guard;
                  func(std::forward<Args>(inner)...);
              },
              std::forward<Args>(args)...)
    {
    }
};
#endif

#ifndef _WIN32
class PThreadWrapperReg : public PThreadWrapper
{
  public:
    PThreadWrapperReg() = default;

    template <typename F, typename... Args>
    explicit PThreadWrapperReg(std::string name, std::string componentTag, F &&f, Args &&...args)
        : PThreadWrapper(
              [n = std::move(name), c = std::move(componentTag), func = std::forward<F>(f)](Args... inner) {
                  AutoRegisterCurrentThread guard(n, c);
                  func(std::forward<Args>(inner)...);
              },
              std::forward<Args>(args)...)
    {
    }

    template <typename F, typename... Args>
    explicit PThreadWrapperReg(F &&f, Args &&...args)
        : PThreadWrapper(
              [func = std::forward<F>(f)](Args... inner) {
                  AutoRegisterCurrentThread guard;
                  func(std::forward<Args>(inner)...);
              },
              std::forward<Args>(args)...)
    {
    }
};
#endif

} // namespace threadschedule
