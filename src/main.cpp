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
#ifdef HAS_CAPTURE
#include "driver_config.h"
#include <fstream>
#endif // HAS_CAPTURE
#include "logger.h"
#include "sysflow_config.h"
#include "sysflowprocessor.h"
#include <csignal>
#include <cstdio>
#include <iostream>
#include <sinsp.h>
#include <string>
#include <unistd.h>

using namespace std;

using namespace sysflowprocessor;

SysFlowProcessor *s_prc = nullptr;

void signal_handler(int /*i*/) { s_prc->exit(); }

int str2int(int &i, char const *s, int base = 0) {
  char *end;
  long l;
  errno = 0;
  l = strtol(s, &end, base);
  if ((errno == ERANGE && l == LONG_MAX) || l > INT_MAX) {
    return -1;
  }
  if ((errno == ERANGE && l == LONG_MIN) || l < INT_MIN) {
    return -1;
  }
  if (*s == '\0' || *end != '\0') {
    return -1;
  }
  i = l;
  return 0;
}

static void usage(const std::string &name) {
  std::cerr
      << "Usage: " << name
      << " [-h] [-G <interval>] [-s <schema file>] [-c] [-e <exporterID>] [-r "
         "<scap file>] [-l <log conf file>] -w <file name/dir>\n"
      << "Options:\n"
      << "\t-h\t\t\tShow this help message\n"
      << "\t-w file name/dir\t(required) The file or directory to which "
         "sysflow records are written\n"
      << "\t\t\t\tIf a directory is specified (using a trailing slash), file "
         "name will be an epoch timestamp\n"
      << "\t\t\t\tIf -G is specified, then the file name specified will have "
         "an epoch timestamp appended to it\n"
      << "\t-e exporterID\t\tA globally unique ID representing the host or VM "
         "being monitored which is stored in the sysflow dumpfile header\n"
      << "\t\t\t\tIf -e not set, the hostname of the CURRENT machine is used,  "
         "which may not be accurate for reading offline scap files\n"
      << "\t-G interval(in secs)\tRotates the dumpfile specified in -w every "
         "interval seconds and appends epoch timestamp to file name\n"
      << "\t-r scap file\t\tThe scap file to be read and dumped as sysflow "
         "format at the file specified by -w\n"
      << "\t\t\t\tIf this option is not specified, a live capture is assumed\n"
      << "\t-s schema file\t\tThe sysflow avro schema file (.avsc) used for "
         "schema validation (default: /usr/local/sysflow/conf/SysFlow.avsc)\n"
      << "\t-f filter\t\tSysdig style filtering string to filter scap. Must be "
         "surrounded by quotes\n"
      << "\t-c\t\t\tSimple, fast filter to allow only container-related events "
         "to be dumped\n"
      << "\t-l log conf file\tLocation of log4cxx properties configuration "
         "file. (default: /usr/local/sysflow/conf/log4cxx.properties)\n"
      << "\t-v\t\t\tPrints the version of " << name << " and exits.\n"
      << std::endl;
}

// static log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("sysflow.main"));
CREATE_MAIN_LOGGER()
int main(int argc, char **argv) {
  string scapFile = "";
  string outputDir;
  string exporterID = "";
  char *duration;
  char c;
  struct sigaction sigIntHandler {};
  string schemaFile = "/usr/local/sysflow/conf/SysFlow.avsc";
  sigIntHandler.sa_handler = signal_handler;
  sigemptyset(&sigIntHandler.sa_mask);
  sigIntHandler.sa_flags = 0;
  bool filterCont = false;
  int fileDuration = 0;
  string filter = "";
  bool help = false;
  string logProps = "/usr/local/sysflow/conf/log4cxx.properties";

  sigaction(SIGINT, &sigIntHandler, nullptr);

  while ((c = getopt(argc, argv, "hcr:w:G:s:e:l:vf:")) != -1) {
    switch (c) {
    case 'e':
      exporterID = optarg;
      break;
    case 'r':
      scapFile = optarg;
      break;
    case 'w':
      outputDir = optarg;
      break;
    case 'G':
      duration = optarg;
      if (str2int(fileDuration, duration, 10)) {
        cout << "Unable to parse " << duration << endl;
        exit(1);
      }
      if (fileDuration < 1) {
        cout << "File duration must be higher than 0" << endl;
        exit(1);
      }
      break;
    case 'c':
      filterCont = true;
      break;
    case 's':
      schemaFile = optarg;
      break;
    case 'f':
      filter = optarg;
      break;
    case 'l':
      logProps = optarg;
      break;
    case 'h':
      help = true;
      break;
    case 'v':
      cerr << " Version: " << SF_VERSION << "." << SF_BUILD << endl;
      exit(0);
    case '?':
      if (optopt == 'r' || optopt == 's' || optopt == 'f' || optopt == 'w' ||
          optopt == 'G' || optopt == 'l') {
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

  if (help) {
    usage(argv[0]);
    return 0;
  }
  if (outputDir.empty()) {
    usage(argv[0]);
    return 1;
  }

  try {
    /*if (!logProps.empty())
    {
        PropertyConfigurator::configure(logProps.c_str());
    }
    else
    {
        BasicConfigurator::configure();
    }*/
    CONFIGURE_LOGGER(logProps);
    SF_DEBUG(logger, "Starting sysporter..");
    auto *cxt = new SysFlowContext(filterCont, fileDuration, outputDir,
                                   scapFile, schemaFile, exporterID, filter);
    s_prc = new SysFlowProcessor(cxt);
    int ret = s_prc->run();
    delete s_prc;
    return ret;
  }
  CATCH_LOGGER_EXCEPTION();
}
