//
//  PlatformIncludes.h
//  Outlaws
//
//  Created by Arthur Danskin on 10/21/12.
//  Copyright (c) 2012-2014 Arthur Danskin. All rights reserved.
//

#ifndef Outlaws_PlatformIncludes_h
#define Outlaws_PlatformIncludes_h

#include <GL/glew.h>
#include <GL/gl.h>
#include <GL/glu.h>

//#include <SDL_opengl.h>

#ifndef __printflike
#define __printflike(X, Y) __attribute__((format(printf, X, Y)))
#endif

#ifndef __has_feature
#define __has_feature(X) 0
#endif

#define HAS_SOUND 1

// use nanosleep in gcc 4.8
#ifndef _GLIBCXX_USE_NANOSLEEP
#define _GLIBCXX_USE_NANOSLEEP 1
#endif

#endif
