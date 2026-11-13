#pragma once

#ifndef NAMESPACE_BEGIN
#define NAMESPACE_BEGIN(name) namespace name {
#endif

#ifndef NAMESPACE_END
#define NAMESPACE_END(name) }
#endif

#ifdef NDEBUG
#define TKP_UNREACHABLE() std::unreachable()
#else
#define TKP_UNREACHABLE() throw std::logic_error("TKP_INVALID_CODE_PATH")
#endif
