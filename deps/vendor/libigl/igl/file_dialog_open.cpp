// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Daniele Panozzo <daniele.panozzo@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "file_dialog_open.h"
#include <cstdio>
#include <cstring>

#ifdef _WIN32
  #include <windows.h>
  #undef max
  #undef min

  #include <Commdlg.h>
#endif

IGL_INLINE std::string igl::file_dialog_open()
{
  const int FILE_DIALOG_MAX_BUFFER = 1024;
  char buffer[FILE_DIALOG_MAX_BUFFER];
  buffer[0] = '\0';
  buffer[FILE_DIALOG_MAX_BUFFER - 1] = 'x';

#ifdef __APPLE__

  FILE * output = popen(
    "osascript -e \""
    "   tell application \\\"System Events\\\"\n"
    "           activate\n"
    "           set existing_file to choose file\n"
    "   end tell\n"
    "   set existing_file_path to (POSIX path of (existing_file))\n"
    "\" 2>/dev/null | tr -d '\n' ","r");
  if (output)
  {
    auto ret = fgets(buffer, FILE_DIALOG_MAX_BUFFER, output);
    if (ret == NULL || ferror(output))
    {

      buffer[0] = '\0';
    }
    if (buffer[FILE_DIALOG_MAX_BUFFER - 1] == '\0')
    {

      buffer[0] = '\0';
    }
  }
#elif defined _WIN32




  OPENFILENAME ofn;
  char szFile[260];


  ZeroMemory(&ofn, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = NULL;
  ofn.lpstrFile = szFile;


  ofn.lpstrFile[0] = '\0';
  ofn.nMaxFile = sizeof(szFile);
  ofn.lpstrFilter = "*.*\0";
  ofn.nFilterIndex = 1;
  ofn.lpstrFileTitle = NULL;
  ofn.nMaxFileTitle = 0;
  ofn.lpstrInitialDir = NULL;
  ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;


  int pos = 0;
  if (GetOpenFileName(&ofn)==TRUE)
  {
    while(ofn.lpstrFile[pos] != '\0')
    {
      buffer[pos] = (char)ofn.lpstrFile[pos];
      pos++;
    }
  }
  buffer[pos] = 0;
#else


  FILE * output = popen("/usr/bin/zenity --file-selection","r");
  if (output)
  {
    auto ret = fgets(buffer, FILE_DIALOG_MAX_BUFFER, output);
    if (ret == NULL || ferror(output))
    {

      buffer[0] = '\0';
    }
    if (buffer[FILE_DIALOG_MAX_BUFFER - 1] == '\0')
    {

      buffer[0] = '\0';
    }
  }


  if(strlen(buffer) > 0)
  {
    buffer[strlen(buffer)-1] = '\0';
  }

#endif
  return std::string(buffer);
}
