/** Copyright (C) 2019 IBM Corporation.
 *
 * Authors:
 * Frederico Araujo <frederico.araujo@ibm.com>
 * Teryl Taylor <terylt@ibm.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 **/

#define __STDC_FORMAT_MACROS
#include "sysflow.h"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include "avro/Compiler.hh"
#include "avro/DataFile.hh"
#include "avro/Decoder.hh"
#include "avro/Encoder.hh"
#include "avro/ValidSchema.hh"
#pragma GCC diagnostic pop
#include "datatypes.h"
#include "op_flags.h"
#include <arpa/inet.h>
#include <ctime>
#define BUFFERSIZE 20

#define HEADER 0
#define CONT 1
#define PROC 2
#define FILE_ 3
#define PROC_EVT 4
#define NET_FLOW 5
#define FILE_FLOW 6
#define FILE_EVT 7

#define NANO_TO_SECS 1000000000

using sysflow::Container;
using sysflow::File;
using sysflow::FileEvent;
using sysflow::FileFlow;
using sysflow::NetworkFlow;
using sysflow::Process;
using sysflow::ProcessEvent;
using sysflow::SFHeader;
using sysflow::SFObjectState;
using sysflow::SysFlow;

Process proc;
ProcessEvent procevt;
SysFlow flow;
SFHeader header;
Container cont;
sysflow::File file;
NetworkFlow netflow;
FileFlow fileflow;
FileEvent fileevt;

typedef google::dense_hash_map<OID *, Process *, MurmurHasher<OID *>, eqoidptr>
    PTable;
typedef google::dense_hash_map<string, sysflow::File *, MurmurHasher<string>,
                               eqstr>
    FTable;
PTable s_procs;
FTable s_files;

bool s_quiet = false;
bool s_keepProcOnExit = false;

const char *Events[] = {"", "CLONE", "EXEC", "", "EXIT", "", "", "", "SETUID"};

avro::ValidSchema loadSchema(const char *filename) {
  avro::ValidSchema result;
  try {
    std::ifstream ifs(filename);
    avro::compileJsonSchema(ifs, result);
  } catch (avro::Exception &ex) {
    cerr << "Unable to load schema from " << filename << endl;
    exit(1);
  }
  return result;
}
void printFileFlow(FileFlow fileflow) {
  string opFlags = "";
  opFlags += ((fileflow.opFlags & OP_OPEN) ? "O" : " ");
  opFlags += ((fileflow.opFlags & OP_ACCEPT) ? "A" : " ");
  opFlags += ((fileflow.opFlags & OP_CONNECT) ? "C" : " ");
  opFlags += ((fileflow.opFlags & OP_WRITE_SEND) ? "W" : " ");
  opFlags += ((fileflow.opFlags & OP_READ_RECV) ? "R" : " ");
  opFlags += ((fileflow.opFlags & OP_SETNS) ? "N" : " ");
  opFlags += ((fileflow.opFlags & OP_CLOSE) ? "C" : " ");
  opFlags += ((fileflow.opFlags & OP_TRUNCATE) ? "T" : " ");
  opFlags += ((fileflow.opFlags & OP_DIGEST) ? "D" : " ");

  time_t startTs = (static_cast<time_t>(fileflow.ts / NANO_TO_SECS));
  time_t endTs = (static_cast<time_t>(fileflow.endTs / NANO_TO_SECS));
  char startTime[100];
  char endTime[100];
  strftime(startTime, 99, "%x %X %Z", localtime(&startTs));
  strftime(endTime, 99, "%x %X %Z", localtime(&endTs));

  string key(fileflow.fileOID.begin(), fileflow.fileOID.end());
  FTable::iterator fi = s_files.find(key);
  if (fi == s_files.end()) {
    std::cout << "Uh Oh! Can't find process for fileflow!! " << endl;
    std::cout << "FILEFLOW " << startTime << " " << endTime << " " << opFlags
              << " TID: " << fileflow.tid << " FD: " << fileflow.fd
              << " WBytes: " << fileflow.numWSendBytes
              << " RBytes: " << fileflow.numRRecvBytes
              << " WOps: " << fileflow.numWSendOps
              << " ROps: " << fileflow.numRRecvOps << " "
              << fileflow.procOID.hpid << " " << fileflow.procOID.createTS
              << endl;
  }

  PTable::iterator it = s_procs.find(&(fileflow.procOID));
  if (it == s_procs.end()) {
    std::cout << "Uh Oh! Can't find process for fileflow!! " << endl;
    std::cout << "FILEFLOW " << startTime << " " << endTime << " " << opFlags
              << " TID: " << fileflow.tid << " FD: " << fileflow.fd
              << " WBytes: " << fileflow.numWSendBytes
              << " RBytes: " << fileflow.numRRecvBytes
              << " WOps: " << fileflow.numWSendOps
              << " ROps: " << fileflow.numRRecvOps << " "
              << fileflow.procOID.hpid << " " << fileflow.procOID.createTS
              << endl;
  } else {
    string container = "";
    if (!it->second->containerId.is_null()) {
      container = it->second->containerId.get_string();
    }
    std::cout << it->second->exe << " " << container << " "
              << it->second->oid.hpid << " " << startTime << " " << endTime
              << " " << opFlags << " Resource: " << fi->second->restype
              << " PATH: " << fi->second->path << " FD: " << fileflow.fd
              << " TID: " << fileflow.tid
              << " Open Flags: " << fileflow.openFlags
              << " WBytes: " << fileflow.numWSendBytes
              << " RBytes: " << fileflow.numRRecvBytes
              << " WOps: " << fileflow.numWSendOps
              << " ROps: " << fileflow.numRRecvOps << " " << it->second->exe
              << " " << it->second->exeArgs << endl;
  }
}

void printFileEvent(FileEvent fileEvt) {
  string opFlags = "";
  opFlags += ((fileEvt.opFlags & OP_MKDIR) ? "MKDIR" : " ");
  opFlags += ((fileEvt.opFlags & OP_RMDIR) ? "RMDIR" : " ");
  opFlags += ((fileEvt.opFlags & OP_LINK) ? "LINK" : " ");
  opFlags += ((fileEvt.opFlags & OP_SYMLINK) ? "SYMLINK" : " ");
  opFlags += ((fileEvt.opFlags & OP_UNLINK) ? "UNLINK" : " ");
  opFlags += ((fileEvt.opFlags & OP_RENAME) ? "RENAME" : " ");
  time_t startTs = (static_cast<time_t>(fileEvt.ts / NANO_TO_SECS));
  char startTime[100];
  strftime(startTime, 99, "%x %X %Z", localtime(&startTs));

  string key(fileEvt.fileOID.begin(), fileEvt.fileOID.end());
  FTable::iterator fi = s_files.find(key);
  if (fi == s_files.end()) {
    std::cout << "Uh Oh! Can't find file for atfileflow!! " << endl;
    std::cout << "FILE_EVT " << startTime << " " << opFlags
              << " TID: " << fileEvt.tid << " " << fileEvt.procOID.hpid << " "
              << fileEvt.procOID.createTS << endl;
  }

  PTable::iterator it = s_procs.find(&(fileEvt.procOID));
  if (it == s_procs.end()) {
    std::cout << "Uh Oh! Can't find process for fileflow!! " << endl;
    std::cout << "FILE_EVT " << startTime << " " << opFlags
              << " TID: " << fileEvt.tid << " " << fileEvt.procOID.hpid << " "
              << fileEvt.procOID.createTS << " " << fi->second->restype << " "
              << fi->second->path << endl;
  } else {
    string container = "";
    if (!it->second->containerId.is_null()) {
      container = it->second->containerId.get_string();
    }
    std::cout << it->second->exe << " " << container << " "
              << it->second->oid.hpid << " " << startTime << " " << opFlags
              << " Resource: " << static_cast<char>(fi->second->restype)
              << " PATH: " << fi->second->path << " TID: " << fileEvt.tid << " "
              << it->second->exe << " " << it->second->exeArgs;
    if (!fileEvt.newFileOID.is_null()) {
      FOID newFileOID = fileEvt.newFileOID.get_FOID();
      string key2(newFileOID.begin(), newFileOID.end());
      FTable::iterator fi2 = s_files.find(key2);
      if (fi2 == s_files.end()) {
        std::cout << "Uh Oh! Can't find file 2 for atfileflow!! " << endl;
      } else {
        std::cout << " PATH 2: " << fi2->second->path << endl;
      }
    } else {
      std::cout << endl;
    }
  }
}

void printNetFlow(NetworkFlow netflow) {
  struct in_addr srcIP {};
  struct in_addr dstIP {};
  srcIP.s_addr = netflow.sip;
  dstIP.s_addr = netflow.dip;
  string opFlags = "";
  opFlags += ((netflow.opFlags & OP_ACCEPT) ? "A" : " ");
  opFlags += ((netflow.opFlags & OP_CONNECT) ? "C" : " ");
  opFlags += ((netflow.opFlags & OP_WRITE_SEND) ? "S" : " ");
  opFlags += ((netflow.opFlags & OP_READ_RECV) ? "R" : " ");
  opFlags += ((netflow.opFlags & OP_CLOSE) ? "C" : " ");
  opFlags += ((netflow.opFlags & OP_TRUNCATE) ? "T" : " ");
  opFlags += ((netflow.opFlags & OP_DIGEST) ? "D" : " ");

  string srcIPStr = string(inet_ntoa(srcIP));
  string dstIPStr = string(inet_ntoa(dstIP));
  time_t startTs = (static_cast<time_t>(netflow.ts / NANO_TO_SECS));
  time_t endTs = (static_cast<time_t>(netflow.endTs / NANO_TO_SECS));
  char startTime[100];
  char endTime[100];
  strftime(startTime, 99, "%x %X %Z", localtime(&startTs));
  strftime(endTime, 99, "%x %X %Z", localtime(&endTs));

  PTable::iterator it = s_procs.find(&(netflow.procOID));
  if (it == s_procs.end()) {
    std::cout << "Uh Oh! Can't find process for netflow!! " << endl;
    std::cout << "NETFLOW " << startTime << " " << endTime << " " << opFlags
              << " TID: " << netflow.tid << " SIP: " << srcIPStr << " "
              << " DIP: " << dstIPStr << " SPORT: " << netflow.sport
              << " DPORT: " << netflow.dport << " PROTO: " << netflow.proto
              << " WBytes: " << netflow.numWSendBytes
              << " RBytes: " << netflow.numRRecvBytes
              << " WOps: " << netflow.numWSendOps
              << " ROps: " << netflow.numRRecvOps << " " << netflow.procOID.hpid
              << " " << netflow.procOID.createTS << endl;
  } else {
    string container = "";
    if (!it->second->containerId.is_null()) {
      container = it->second->containerId.get_string();
    }
    std::cout << it->second->exe << " " << container << " "
              << it->second->oid.hpid << " " << startTime << " " << endTime
              << " " << opFlags << " TID: " << netflow.tid
              << " SIP: " << srcIPStr << " "
              << " DIP: " << dstIPStr << " SPORT: " << netflow.sport
              << " DPORT: " << netflow.dport << " PROTO: " << netflow.proto
              << " WBytes: " << netflow.numWSendBytes
              << " RBytes: " << netflow.numRRecvBytes
              << " WOps: " << netflow.numWSendOps
              << " ROps: " << netflow.numRRecvOps << " " << it->second->exe
              << " " << it->second->exeArgs << endl;
  }
}

sysflow::File *createFile(const sysflow::File &file) {
  auto *f = new sysflow::File();
  f->state = file.state;
  f->ts = file.ts;
  f->restype = file.restype;
  f->path = file.path;
  if (!file.containerId.is_null()) {
    f->containerId.set_string(file.containerId.get_string());
  } else {
    f->containerId.set_null();
  }
  return f;
}

Process *createProcess(const Process &proc) {
  auto *p = new Process();
  p->state = proc.state;
  p->ts = proc.ts;
  p->oid.hpid = proc.oid.hpid;
  p->oid.createTS = proc.oid.createTS;

  if (!proc.poid.is_null()) {
    OID poid = proc.poid.get_OID();
    p->poid.set_OID(poid);
  }
  p->exe = proc.exe;
  p->exeArgs = proc.exeArgs;
  p->uid = proc.uid;
  p->gid = proc.gid;
  p->userName = proc.userName;
  p->groupName = proc.groupName;
  if (!proc.containerId.is_null()) {
    p->containerId.set_string(proc.containerId.get_string());
  } else {
    p->containerId.set_null();
  }
  return p;
}

int runEventLoop(const string &sysFile, const string &schemaFile) {
  std::cout << "Loading sys file " << sysFile << endl;
  avro::ValidSchema sysfSchema = loadSchema(schemaFile.c_str());
  avro::DataFileReader<SysFlow> dfr(sysFile.c_str(), sysfSchema);
  int i = 0;
  while (dfr.read(flow)) {
    switch (flow.rec.idx()) {
    case HEADER: {
      header = flow.rec.get_SFHeader();
      std::cout << "Version: " << header.version
                << " Exporter: " << header.exporter << endl;
      break;
    }
    case PROC: {
      proc = flow.rec.get_Process();
      if (!s_quiet) {
        std::cout << "PROC " << proc.oid.hpid << " " << proc.oid.createTS << " "
                  << proc.ts << " " << proc.state << " " << proc.exe << " "
                  << proc.exeArgs << " " << proc.oid.hpid << " "
                  << proc.userName << " " << proc.oid.createTS
                  << " TTY: " << proc.tty;
        if (!proc.poid.is_null()) {
          std::cout << " Parent: " << proc.poid.get_OID().hpid << " "
                    << proc.poid.get_OID().createTS;
        }
        if (!proc.containerId.is_null()) {
          std::cout << " Container ID: " << proc.containerId.get_string()
                    << endl;
        } else {
          std::cout << endl;
        }
      }
      PTable::iterator it = s_procs.find(&(proc.oid));
      if (it != s_procs.end() && proc.state != SFObjectState::MODIFIED) {
        std::cout << "Uh oh! Process " << it->second->exe
                  << " already in the process table PID: "
                  << it->second->oid.hpid
                  << " Create TS: " << it->second->oid.createTS << endl;
      } else {
        Process *p = createProcess(proc);
        if (it != s_procs.end()) {
          Process *oldP = it->second;
          s_procs.erase(&(oldP->oid));
          delete oldP;
        }
        s_procs[&(p->oid)] = p;
      }

      break;
    }
    case FILE_: {
      file = flow.rec.get_File();
      sysflow::File *f = createFile(file);
      string key(file.oid.begin(), file.oid.end());
      FTable::iterator it = s_files.find(key);
      std::cout << "FILE: " << f->path << " " << f->ts << " " << f->state << " "
                << static_cast<char>(f->restype) << endl;
      if (it != s_files.end()) {
        std::cout << "Uh oh!  File:  " << f->path
                  << " already exists in the sysflow file" << endl;
      }
      s_files[key] = f;
      break;
    }
    case PROC_EVT: {
      procevt = flow.rec.get_ProcessEvent();
      time_t timestamp = (static_cast<time_t>(procevt.ts / NANO_TO_SECS));
      char times[100];
      strftime(times, 99, "%x %X %Z", localtime(&timestamp));
      PTable::iterator it = s_procs.find(&(procevt.procOID));
      if (it == s_procs.end()) {
        std::cout << "Can't find process for process flow!  shouldn't happen!!"
                  << endl;
        std::cout << "PROC_EVT " << times << " "
                  << " TID: " << procevt.tid << Events[procevt.opFlags] << " "
                  << " " << procevt.ret << " OID: " << procevt.procOID.hpid
                  << " " << procevt.procOID.createTS << endl;
      } else {

        string container = "";
        if (!it->second->containerId.is_null()) {
          container = it->second->containerId.get_string();
        }

        std::cout << it->second->exe << " " << container << " "
                  << it->second->oid.hpid << " " << times << " "
                  << " TID: " << procevt.tid << " " << Events[procevt.opFlags]
                  << " "
                  << " " << procevt.ret << " " << procevt.procOID.createTS
                  << " " << it->second->exe << " " << it->second->exeArgs;
        if (procevt.args.empty()) {
          std::cout << endl;
        } else {
          std::cout << " " << procevt.args.back() << endl;
        }
      }
      if (!s_keepProcOnExit && procevt.opFlags == OP_EXIT) { // exit
        if (it != s_procs.end()) {
          if (it->second->oid.hpid == procevt.tid) {
            delete it->second;
            s_procs.erase(&(procevt.procOID));
          }
        }
      }

      break;
    }
    case CONT: {
      cont = flow.rec.get_Container();
      if (!s_quiet) {
        std::cout << "CONT Name: " << cont.name << " ID: " << cont.id
                  << " Image: " << cont.image << " Image ID: " << cont.imageid
                  << " Type: " << cont.type << "Privileged:" << cont.privileged
                  << endl;
      }
      break;
    }
    case NET_FLOW: {
      netflow = flow.rec.get_NetworkFlow();
      printNetFlow(netflow);
      break;
    }
    case FILE_FLOW: {
      fileflow = flow.rec.get_FileFlow();
      printFileFlow(fileflow);
      break;
    }
    case FILE_EVT: {
      fileevt = flow.rec.get_FileEvent();
      printFileEvent(fileevt);
      break;
    }
    default: {
      std::cout << "No sysflow union type has object mapping to index "
                << flow.rec.idx() << endl;
    }
    }
    i++;
  }
  std::cout << "Number of records: " << i << endl;
  return 0;
}

int main(int argc, char **argv) {
  string sysFile;
  string schemaFile = "/usr/local/sysflow/conf/SysFlow.avsc";
  char c;
  OID empkey;
  OID delkey;
  empkey.hpid = 0;
  empkey.createTS = 0;
  s_procs.set_empty_key(&empkey);
  delkey.hpid = 1;
  delkey.createTS = 1;
  s_procs.set_deleted_key(&delkey);
  s_files.set_empty_key("-1");
  s_files.set_deleted_key("-2");
  while ((c = static_cast<char>(getopt(argc, argv, "lr:w:s:qk"))) != -1) {
    switch (c) {
    case 'r':
      sysFile = optarg;
      break;
    case 's':
      schemaFile = optarg;
      break;
    case 'q':
      s_quiet = true;
      break;
    case 'k':
      s_keepProcOnExit = true;
      break;
    case '?':
      if (optopt == 'r' || optopt == 'm') {
        fprintf(stderr, "Option -%c requires an argument.\n", optopt);
      } else if (isprint(optopt)) {
        fprintf(stderr, "Unknown option `-%c'.\n", optopt);
      } else {
        fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
      }
      return 1;
    default:
      abort();
    }
  }
  return runEventLoop(sysFile, schemaFile);
}
