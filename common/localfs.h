/*****************************************************************************
Copyright 2005 - 2010 The Board of Trustees of the University of Illinois.

Licensed under the Apache License, Version 2.0 (the "License"); you may not
use this file except in compliance with the License. You may obtain a copy of
the License at

   http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
License for the specific language governing permissions and limitations under
the License.
*****************************************************************************/

/*****************************************************************************
written by
   Yunhong Gu, last updated 08/19/2010
*****************************************************************************/


#ifndef __SECTOR_LOCALFS_H__
#define __SECTOR_LOCALFS_H__

class LocalFS
{
public:
   static int mkdir(const std::string& path);
   static int rmdir(const std::string& path);
   static int clean_dir(const std::string& path);
   static int list_dir(const std::string& path, std::vector<SNode>& filelist);
   static int stat(const std::string& path, SNode& s);

private:
   static std::string reviseSysCmdPath(const std::string& path);
};

class LinuxFS: public LocalFS;
class WinFS: public localFS;

//class otherFS;

#endif
