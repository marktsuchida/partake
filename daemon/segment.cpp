/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "segment.hpp"

#include "overloaded.hpp"
#include "shmem_mmap.hpp"
#include "shmem_sysv.hpp"
#include "shmem_win32.hpp"

#include <cassert>
#include <exception>

namespace partake::daemon {

namespace {

class unsupported_segment final : public internal::segment_impl {
    std::size_t siz;

  public:
    explicit unsupported_segment(std::size_t size) : siz(size) {}

    template <typename Cfg>
    explicit unsupported_segment([[maybe_unused]] Cfg const &cfg,
                                 std::size_t size)
        : siz(size) {}

    [[nodiscard]] auto is_valid() const noexcept -> bool override {
        return false;
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t override {
        return siz;
    }

    [[nodiscard]] auto spec() const -> segment_spec override {
        assert(false);
        std::terminate();
    }
};

#ifndef _WIN32

class posix_mmap_segment final : public internal::segment_impl {
    mmap_shmem shm;

  public:
    explicit posix_mmap_segment(posix_mmap_segment_config const &cfg,
                                std::size_t size)
        : shm(cfg.name.empty()
                  ? create_posix_mmap_shmem(size)
                  : create_posix_mmap_shmem(cfg.name, size, cfg.force)) {}

    [[nodiscard]] auto is_valid() const noexcept -> bool override {
        return shm.is_valid();
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t override {
        return shm.size();
    }

    [[nodiscard]] auto spec() const -> segment_spec override {
        return {posix_mmap_segment_spec{shm.name()}, size()};
    }
};

class file_mmap_segment final : public internal::segment_impl {
    mmap_shmem shm;

  public:
    explicit file_mmap_segment(file_mmap_segment_config const &cfg,
                               std::size_t size)
        : shm([&]() {
              if (cfg.filename.empty())
                  return create_file_mmap_shmem(size);
              std::error_code ec;
              auto canon = std::filesystem::weakly_canonical(cfg.filename, ec);
              if (ec) {
                  spdlog::error("{}: Cannot get canonical path: {} ({})",
                                cfg.filename, ec.message(), ec.value());
                  return mmap_shmem();
              }
              return create_file_mmap_shmem(canon, size, cfg.force);
          }()) {}

    [[nodiscard]] auto is_valid() const noexcept -> bool override {
        return shm.is_valid();
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t override {
        return shm.size();
    }

    [[nodiscard]] auto spec() const -> segment_spec override {
        return {file_mmap_segment_spec{shm.name()}, size()};
    }
};

class sysv_segment final : public internal::segment_impl {
    sysv_shmem shm;

  public:
    explicit sysv_segment(sysv_segment_config const &cfg, std::size_t size)
        : shm(cfg.key == 0 ? create_sysv_shmem(size, cfg.use_huge_pages,
                                               cfg.huge_page_size)
                           : create_sysv_shmem(cfg.key, size, cfg.force,
                                               cfg.use_huge_pages,
                                               cfg.huge_page_size)) {}

    [[nodiscard]] auto is_valid() const noexcept -> bool override {
        return shm.is_valid();
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t override {
        return shm.size();
    }

    [[nodiscard]] auto spec() const -> segment_spec override {
        return {sysv_segment_spec{shm.id()}, size()};
    }
};

using win32_segment = unsupported_segment;

#else // _WIN32

using posix_mmap_segment = unsupported_segment;
using file_mmap_segment = unsupported_segment;
using sysv_segment = unsupported_segment;

class win32_segment final : public internal::segment_impl {
    std::string mapping_name;
    win32_shmem shm;
    bool large_pages;

  public:
    explicit win32_segment(win32_segment_config const &cfg, std::size_t size)
        : mapping_name(cfg.name.empty() ? generate_win32_file_mapping_name()
                                        : cfg.name),
          shm([&]() {
              if (cfg.filename.empty()) {
                  return create_win32_shmem(mapping_name, size,
                                            cfg.use_large_pages);
              }
              std::error_code ec;
              auto canon = std::filesystem::weakly_canonical(cfg.filename, ec);
              if (ec) {
                  spdlog::error("{}: Cannot get canonical path: {} ({})",
                                cfg.filename, ec.message(), ec.value());
                  return win32_shmem();
              }
              return create_win32_file_shmem(canon, mapping_name, size,
                                             cfg.force, cfg.use_large_pages);
          }()),
          large_pages(cfg.use_large_pages) {}

    [[nodiscard]] auto is_valid() const noexcept -> bool override {
        return shm.is_valid();
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t override {
        return shm.size();
    }

    [[nodiscard]] auto spec() const -> segment_spec override {
        return {win32_segment_spec{mapping_name, large_pages}, size()};
    }
};

#endif // _WIN32

} // namespace

segment::segment() : impl(std::make_unique<unsupported_segment>(0)) {}

segment::segment(segment_config const &config)
    : impl(std::visit(
          common::overloaded{
              [&config](posix_mmap_segment_config const &cfg) -> impl_ptr {
                  return std::make_unique<posix_mmap_segment>(cfg,
                                                              config.size);
              },
              [&config](file_mmap_segment_config const &cfg) -> impl_ptr {
                  return std::make_unique<file_mmap_segment>(cfg, config.size);
              },
              [&config](sysv_segment_config const &cfg) -> impl_ptr {
                  return std::make_unique<sysv_segment>(cfg, config.size);
              },
              [&config](win32_segment_config const &cfg) -> impl_ptr {
                  return std::make_unique<win32_segment>(cfg, config.size);
              },
          },
          config.method)) {}

} // namespace partake::daemon
