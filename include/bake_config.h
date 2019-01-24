/*
                                   )
                                  (.)
                                  .|.
                                  | |
                              _.--| |--._
                           .-';  ;`-'& ; `&.
                          \   &  ;    &   &_/
                           |"""---...---"""|
                           \ | | | | | | | /
                            `---.|.|.|.---'

 * This file is generated by bake.lang.c for your convenience. Headers of
 * dependencies will automatically show up in this file. Include bake_config.h
 * in your main project file. Do not edit! */

#ifndef CORTO_WS_BAKE_CONFIG_H
#define CORTO_WS_BAKE_CONFIG_H

/* Headers of public dependencies */
#include <base64>
#include <corto.httpserver>
#include <corto.range>
#include <tags>
#include <corto>
#include <corto.httpserver.c>
#include <corto.range.c>
#include <tags.c>
#include <corto.c>
#include <bake.util>

/* Headers of private dependencies */
#ifdef CORTO_WS_IMPL
/* No dependencies */
#endif

/* Convenience macro for exporting symbols */
#if CORTO_WS_IMPL && defined _MSC_VER
#define CORTO_WS_EXPORT __declspec(dllexport)
#elif CORTO_WS_IMPL
#define CORTO_WS_EXPORT __attribute__((__visibility__("default")))
#elif defined _MSC_VER
#define CORTO_WS_EXPORT __declspec(dllimport)
#else
#define CORTO_WS_EXPORT
#endif

#endif

