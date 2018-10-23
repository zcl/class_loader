/*
 * Copyright (c) 2012, Willow Garage, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Willow Garage, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "class_loader/class_loader.hpp"

#include <Poco/SharedLibrary.h>

#include <string>

namespace class_loader
{

bool ClassLoader::has_unmananged_instance_been_created_ = false;

bool ClassLoader::hasUnmanagedInstanceBeenCreated()
{
  return ClassLoader::has_unmananged_instance_been_created_;
}

void ClassLoader::setUnmanagedInstanceBeenCreated(bool state)
{
  ClassLoader::has_unmananged_instance_been_created_ = state;
}

std::string systemLibraryPrefix()
{
#ifndef _WIN32
  return "lib";
#endif
  return "";
}

std::string systemLibrarySuffix()
{
// Poco should be compiled with `#define POCO_NO_SHARED_LIBRARY_DEBUG_SUFFIX`
// to automatically remove the trailing `d` from the shared library suffix
//   return Poco::SharedLibrary::suffix();
#ifdef __linux__
  return ".so";
#elif __APPLE__
  return ".dylib";
#elif _WIN32
  return ".dll";
#else
  return Poco::SharedLibrary::suffix();
#endif
}

std::string systemLibraryFormat(const std::string & library_name)
{
  return systemLibraryPrefix() + library_name + systemLibrarySuffix();
}

ClassLoader::ClassLoader(const std::string & library_path, bool ondemand_load_unload)
: ondemand_load_unload_(ondemand_load_unload),
  library_path_(library_path),
  load_ref_count_(0),
  plugin_ref_count_(0)
{
  CONSOLE_BRIDGE_logDebug(
    "class_loader.ClassLoader: "
    "Constructing new ClassLoader (%p) bound to library %s.",
    this, library_path.c_str());
  if (!isOnDemandLoadUnloadEnabled()) {
    loadLibrary();
  }
}

ClassLoader::~ClassLoader()
{
  CONSOLE_BRIDGE_logDebug(
    "%s",
    "class_loader.ClassLoader: Destroying class loader, "
    "unloading associated library...\n");
  unloadLibrary();  // TODO(mikaelarguedas): while(unloadLibrary() > 0){} ??
}

const std::string & ClassLoader::getLibraryPath() const
{
  return library_path_;
}

bool ClassLoader::isLibraryLoaded() const
{
  return class_loader::impl::isLibraryLoaded(getLibraryPath(), this);
}

bool ClassLoader::isLibraryLoadedByAnyClassloader() const
{
  return class_loader::impl::isLibraryLoadedByAnybody(getLibraryPath());
}

bool ClassLoader::isOnDemandLoadUnloadEnabled() const
{
  return ondemand_load_unload_;
}

void ClassLoader::loadLibrary()
{
  if (getLibraryPath() == "") {
    // special library path for libraries linked at link-time (not dlopen-ed)
    return;
  }
  std::lock_guard<std::recursive_mutex> lock(load_ref_count_mutex_);
  ++load_ref_count_;
  class_loader::impl::loadLibrary(getLibraryPath(), this);
}

int ClassLoader::unloadLibrary()
{
  if (getLibraryPath() == "") {
    // special library path for libraries linked at link-time (not dlopen-ed)
    return 0;
  }
  return unloadLibraryInternal(true);
}

int ClassLoader::unloadLibraryInternal(bool lock_plugin_ref_count)
{
  std::lock_guard<std::recursive_mutex> load_ref_lock(load_ref_count_mutex_);
  if (lock_plugin_ref_count) {
    plugin_ref_count_mutex_.lock();
  }

  try {
    if (plugin_ref_count_ > 0) {
      CONSOLE_BRIDGE_logWarn("class_loader.ClassLoader: SEVERE WARNING!!!\n"
                             "Attempting to unload %s\n"
                             "while objects created by this library still exist in the heap!\n"
                             "You should delete your objects before destroying the ClassLoader. "
                             "The library will NOT be unloaded.", library_path_.c_str());
    } else {
      load_ref_count_ = load_ref_count_ - 1;
      if (load_ref_count_ == 0) {
        class_loader::impl::unloadLibrary(getLibraryPath(), this);
      } else if (load_ref_count_ < 0) {
        load_ref_count_ = 0;
      }
    }
  } catch (...) {
    if (lock_plugin_ref_count) {
      plugin_ref_count_mutex_.unlock();
    }
    throw;
  }
  if (lock_plugin_ref_count) {
    plugin_ref_count_mutex_.unlock();
  }
  return load_ref_count_;
}

}  // namespace class_loader
