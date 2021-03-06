/* initexmf.cpp: MiKTeX configuration utility

   Copyright (C) 1996-2018 Christian Schenk

   This file is part of IniTeXMF.

   IniTeXMF is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   IniTeXMF is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with IniTeXMF; if not, write to the Free Software Foundation,
   59 Temple Place - Suite 330, Boston, MA 02111-1307, USA. */

#include "StdAfx.h"

using namespace MiKTeX::Core;
using namespace MiKTeX::Packages;
using namespace MiKTeX::Trace;
using namespace MiKTeX::Util;
using namespace MiKTeX::Wrappers;
using namespace std;
using namespace std::string_literals;

#define UNUSED_ALWAYS(x)

#define UNIMPLEMENTED() MIKTEX_INTERNAL_ERROR()

#define T_(x) MIKTEXTEXT(x)

#define Q_(x) MiKTeX::Core::Quoter<char>(x).GetData()

const char* const TheNameOfTheGame = T_("MiKTeX Configuration Utility");

#define PROGNAME "initexmf"

static log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger(PROGNAME));
static bool isLog4cxxConfigured = false;

template<class VALTYPE> class AutoRestore
{
public:
  AutoRestore(VALTYPE& val) :
    oldVal(val),
    pVal(&val)
  {
  }

public:
  ~AutoRestore()
  {
    *pVal = oldVal;
  }

private:
  VALTYPE oldVal;

private:
  VALTYPE* pVal;
};

enum class LinkType
{
  Hard,
  Symbolic,
  Copy
};

struct FileLink
{
  FileLink(const string& target, const vector<string>& linkNames) :
    target(target),
    linkNames(linkNames)
  {
  }
  FileLink(const string& target, const vector<string>& linkNames, LinkType linkType) :
    target(target),
    linkNames(linkNames),
    linkType(linkType)
  {
  }
  string target;
  vector<string> linkNames;
#if defined(MIKTEX_WINDOWS)
  LinkType linkType = LinkType::Hard;
#else
  LinkType linkType = LinkType::Symbolic;
#endif
};

enum class LinkCategory
{
  Formats,
  MiKTeX,
  Scripts
};

typedef OptionSet<LinkCategory> LinkCategoryOptions;

class XmlWriter
{
public:
  void StartDocument()
  {
    cout << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << endl;
  }

public:
  void StartElement(const string& name)
  {
    if (freshElement)
    {
      cout << ">";
    }
    cout << "<" << name;
    freshElement = true;
    elements.push(name);
  }

public:
  void AddAttribute(const string& attributeName, const string& attributeValue)
  {
    cout << " " << attributeName << "=\"" << attributeValue << "\"";
  }

public:
  void EndElement()
  {
    if (elements.empty())
    {
      MIKTEX_FATAL_ERROR(T_("No elements on the stack."));
    }
    if (freshElement)
    {
      cout << "/>";
      freshElement = false;
    }
    else
    {
      cout << "</" << elements.top() << ">";
    }
    elements.pop();
  }

public:
  void EndAllElements()
  {
    while (!elements.empty())
    {
      EndElement();
    }
  }

public:
  void Text(const string& text)
  {
    if (freshElement)
    {
      cout << ">";
      freshElement = false;
    }
    for (char ch : text)
    {
      switch (ch)
      {
      case '&':
        cout <<"&amp;";
        break;
      case '<':
        cout << "&lt;";
        break;
      case '>':
        cout << "&gt;";
        break;
      default:
        cout << ch;
        break;
      }
    }
  }

private:
  stack<string> elements;

private:
  bool freshElement = false;
};

static struct
{
  const char* lpszShortcut;
  const char* lpszFile;
}
configShortcuts[] = {
  "pdftex", MIKTEX_PATH_PDFTEX_CFG,
  "dvips", MIKTEX_PATH_CONFIG_PS,
  "dvipdfmx", MIKTEX_PATH_DVIPDFMX_CONFIG,
  "updmap", MIKTEX_PATH_UPDMAP_CFG,
};

string Timestamp()
{
  auto now = time(nullptr);
  stringstream s;
  s << std::put_time(localtime(&now), "%Y-%m-%d-%H%M%S");
  return s.str();
}

class IniTeXMFApp :
  public IFindFileCallback,
  public ICreateFndbCallback,
  public IEnumerateFndbCallback,
  public PackageInstallerCallback,
  public TraceCallback
{
public:
  IniTeXMFApp();

public:
  ~IniTeXMFApp();

public:
  PathName GetLogDir()
  {
    return session->GetSpecialPath(SpecialPath::LogDirectory);
  }

public:
  string GetLogName()
  {
    string logName = "initexmf";
    if (session->IsAdminMode() && session->RunningAsAdministrator())
    {
      logName += MIKTEX_ADMIN_SUFFIX;
    }
    return logName;
  }

public:
  void Init(int argc, const char* argv[]);

public:
  void Finalize(bool keepSession);

private:
  void Verbose(const char* lpszFormat, ...);

private:
  void PrintOnly(const char* lpszFormat, ...);

private:
  void Warning(const char* lpszFormat, ...);

private:
  MIKTEXNORETURN void FatalError(const char* lpszFormat, ...);

private:
  void UpdateFilenameDatabase(const PathName& root);

private:
  void UpdateFilenameDatabase(unsigned root);

private:
  void ListFormats();

private:
  void ListMetafontModes();

private:
  void Clean();

private:
  void RemoveFndb();

private:
#if defined(MIKTEX_WINDOWS)
  void SetTeXMFRootDirectories(bool noRegistry);
#else
  void SetTeXMFRootDirectories();
#endif

private:
  void RunProcess(const PathName& fileName, const vector<string>& arguments)
  {
    ProcessOutput<4096> output;
    int exitCode;
    if (!Process::Run(fileName, arguments, &output, &exitCode, nullptr) || exitCode != 0)
    {
      auto outputBytes = output.GetStandardOutput();
      PathName outfile = GetLogDir() / fileName.GetFileNameWithoutExtension();
      outfile += "_";
      outfile += Timestamp().c_str();
      outfile.SetExtension(".out");
      FileStream outstream(File::Open(outfile, FileMode::Create, FileAccess::Write, false));
      outstream.Write(&outputBytes[0], outputBytes.size());
      outstream.Close();
      MIKTEX_FATAL_ERROR_2(T_("The executed process did not succeed. The process output has been saved to a file."), "fileName", fileName.ToString(), "exitCode", std::to_string(exitCode), "savedOutput", outfile.ToString());
    }
  }

private:
  void RunMakeTeX(const string& makeProg, const vector<string>& arguments);

private:
  void MakeFormatFile(const string& formatKey);

private:
  void MakeFormatFiles(const vector<string>& formats);

private:
  void MakeFormatFilesByName(const vector<string>& formatsByName, const string& engine);

private:
  void MakeMaps(bool force);

private:
  void CreateConfigFile(const string& relPath, bool edit);

private:
  void SetConfigValue(const string& valueSpec);

private:
  void ShowConfigValue(const string& valueSpec);

private:
  vector<FileLink> CollectLinks(LinkCategoryOptions linkCategories);

private:
  void ManageLinks(LinkCategoryOptions linkCategories, bool remove, bool force);

#if defined(MIKTEX_UNIX)
private:
  void MakeScriptsExecutable();
#endif

private:
  void MakeLanguageDat(bool force);

private:
  void RegisterRoots(const vector<PathName>& roots, bool other, bool reg);

#if defined(MIKTEX_WINDOWS)
private:
  void RegisterShellFileTypes(bool reg);
#endif

private:
  void ModifyPath();

private:
  void ManageLink(const FileLink& fileLink, bool supportsHardLinks, bool remove, bool overwrite);

private:
  void ReportMiKTeXVersion();

private:
  void ReportOSVersion();

private:
  void ReportRoots();

private:
  void ReportFndbFiles();

#if defined(MIKTEX_WINDOWS)
private:
  void ReportEnvironmentVariables();
#endif

private:
  void ReportBrokenPackages();

private:
  void WriteReport();

private:
  void Bootstrap();

private:
  struct OtherTeX
  {
    string name;
    string version;
    StartupConfig startupConfig;
  };

private:
  vector<OtherTeX> FindOtherTeX();

private:
  void RegisterOtherRoots();

private:
  void CreatePortableSetup(const PathName& portableRoot);

public:
  void Run(int argc, const char* argv[]);

private:
  void FindWizards();

private:
  bool InstallPackage(const string& deploymentName, const PathName& trigger, PathName& installRoot) override;

private:
  bool TryCreateFile(const MiKTeX::Core::PathName& fileName, MiKTeX::Core::FileType fileType) override;

private:
  bool ReadDirectory(const PathName& path, vector<string>& subDirNames, vector<string>& fileNames, vector<string>& fileNameInfos) override;

private:
  bool OnProgress(unsigned level, const PathName& directory) override;

private:
  bool OnFndbItem(const PathName& parent, const string& name, const string& info, bool isDirectory) override;

public:
  void ReportLine(const string& str) override;
  
public:
  bool OnRetryableError(const string& message) override;
  
public:
  bool OnProgress(Notification nf) override;

private:
  vector<TraceCallback::TraceMessage> pendingTraceMessages;

private:
  void PushTraceMessage(const TraceCallback::TraceMessage& traceMessage)
  {
    if (pendingTraceMessages.size() > 100)
    {
      pendingTraceMessages.clear();
    }
    pendingTraceMessages.push_back(traceMessage);
  }

private:
  void PushTraceMessage(const string& message)
  {
    PushTraceMessage(TraceCallback::TraceMessage("initexmf", "initexmf", message));
  }
  
public:
  void Trace(const TraceCallback::TraceMessage& traceMessage) override
  {
    if (!isLog4cxxConfigured)
    {
#if 0
      fprintf(stderr, "%s\n", traceMessage.message.c_str());
#endif
      PushTraceMessage(traceMessage);
      return;
    }
    FlushPendingTraceMessages();
    LogTraceMessage(traceMessage);
  }

private:
  void FlushPendingTraceMessages()
  {
    for (const TraceCallback::TraceMessage& msg : pendingTraceMessages)
    {
      if (isLog4cxxConfigured)
      {
        LogTraceMessage(msg);
      }
      else
      {
        cerr << msg.message << endl;
      }
    }
    pendingTraceMessages.clear();
  }

private:
  void LogTraceMessage(const TraceCallback::TraceMessage& traceMessage)
  {
    MIKTEX_ASSERT(isLog4cxxConfigured);
    log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger(string("trace.initexmf.") + traceMessage.facility);
    if (traceMessage.streamName == MIKTEX_TRACE_ERROR)
    {
      LOG4CXX_ERROR(logger, traceMessage.message);
    }
    else
    {
      LOG4CXX_TRACE(logger, traceMessage.message);
    }
  }

private:
  void EnsureInstaller()
  {
    if (packageInstaller == nullptr)
    {
      packageInstaller = packageManager->CreateInstaller();
      packageInstaller->SetCallback(this);
      packageInstaller->SetNoPostProcessing(true);
    }
  }

private:
  PathName enumDir;

private:
  bool csv = false;

private:
  bool xml = false;

private:
  bool recursive = false;

private:
  bool verbose = false;

private:
  bool quiet = false;

private:
  bool printOnly = false;

private:
  bool removeFndb = false;

private:
  StartupConfig startupConfig;

private:
  vector<string> formatsMade;

private:
  StreamWriter logStream;

private:
  bool updateWizardRunning = false;

private:
  bool setupWizardRunning = false;

private:
  bool isMktexlsrMode = false;

private:
  bool isTexlinksMode = false;
  
private:
  std::shared_ptr<MiKTeX::Packages::PackageManager> packageManager;

private:
  std::shared_ptr<MiKTeX::Packages::PackageInstaller> packageInstaller;

private:
  TriState enableInstaller = TriState::Undetermined;

private:
  std::shared_ptr<MiKTeX::Core::Session> session;

private:
  XmlWriter xmlWriter;

private:
  static const struct poptOption aoption_user[];

private:
  static const struct poptOption aoption_setup[];

private:
  static const struct poptOption aoption_update[];

private:
  static const struct poptOption aoption_mktexlsr[];

private:
  static const struct poptOption aoption_texlinks[];
};

enum Option
{
  OPT_AAA = 256,

  OPT_ADMIN,
  OPT_DISABLE_INSTALLER,
  OPT_DUMP,
  OPT_DUMP_BY_NAME,
  OPT_EDIT_CONFIG_FILE,
  OPT_ENABLE_INSTALLER,
  OPT_ENGINE,
  OPT_FORCE,
  OPT_LIST_MODES,
  OPT_MKLINKS,
  OPT_MKMAPS,
  OPT_PRINT_ONLY,
  OPT_REGISTER_ROOT,
  OPT_REMOVE_LINKS,
  OPT_QUIET,
  OPT_UNREGISTER_ROOT,
  OPT_REPORT,
  OPT_UPDATE_FNDB,
  OPT_USER_ROOTS,
  OPT_VERBOSE,
  OPT_VERSION,

  OPT_ADD_FILE,                 // <experimental/>
  OPT_CLEAN,                    // <experimental/>
  OPT_CREATE_CONFIG_FILE,       // <experimental/>
  OPT_CSV,                      // <experimental/>
  OPT_FIND_OTHER_TEX,           // <experimental/>
  OPT_LIST_DIRECTORY,           // <experimental/>
  OPT_LIST_FORMATS,             // <experimental/>
  OPT_MODIFY_PATH,              // <experimental/>
  OPT_RECURSIVE,                // <experimental/>
  OPT_REGISTER_OTHER_ROOTS,     // <experimental/>
  OPT_REGISTER_SHELL_FILE_TYPES,        // <experimental/>
  OPT_REMOVE_FILE,              // <experimental/>
  OPT_SET_CONFIG_VALUE,         // <experimental/>
  OPT_SHOW_CONFIG_VALUE,                // <experimental/>
  OPT_UNREGISTER_SHELL_FILE_TYPES,      // <experimental/>
  OPT_XML,                      // <experimental/>

  OPT_COMMON_CONFIG,            // <internal/>
  OPT_COMMON_DATA,              // <internal/>
  OPT_COMMON_INSTALL,           // <internal/>
  OPT_COMMON_ROOTS,             // <internal/>
  OPT_MKLANGS,                  // <internal/>
  OPT_LOG_FILE,                 // <internal/>
  OPT_DEFAULT_PAPER_SIZE,       // <internal/>
#if defined(MIKTEX_WINDOWS)
  OPT_NO_REGISTRY,              // <internal/>
#endif
  OPT_PORTABLE,                 // <internal/>
  OPT_RMFNDB,                   // <internal/>
  OPT_USER_CONFIG,              // <internal/>
  OPT_USER_DATA,                // <internal/>
  OPT_USER_INSTALL,             // <internal/>
};

#include "aoption_user.h"
#include "aoption_setup.h"
#include "aoption_update.h"

const struct poptOption IniTeXMFApp::aoption_mktexlsr[] = {
  {
    "dry-run", 0,
    POPT_ARG_NONE, nullptr,
    OPT_PRINT_ONLY,
    T_("Print what would be done."),
    nullptr
  },

  {
    "quiet", 0,
    POPT_ARG_NONE, nullptr,
    OPT_QUIET,
    T_("Suppress screen output."),
    nullptr
  },

  {
    "silent", 0,
    POPT_ARG_NONE, nullptr,
    OPT_QUIET,
    T_("Same as --quiet."),
    nullptr
  },

  {
    "verbose", 0,
    POPT_ARG_NONE, nullptr,
    OPT_VERBOSE,
    T_("Print information on what is being done."),
    nullptr
  },

  {
    "version", 0,
    POPT_ARG_NONE, nullptr,
    OPT_VERSION,
    T_("Print version information and exit."),
    nullptr
  },

  POPT_AUTOHELP
  POPT_TABLEEND
};

const struct poptOption IniTeXMFApp::aoption_texlinks[] = {
  {
    "quiet", 'q',
    POPT_ARG_NONE, nullptr,
    OPT_QUIET,
    T_("Suppress screen output."),
    nullptr
  },

  {
    "silent", 's',
    POPT_ARG_NONE, nullptr,
    OPT_QUIET,
    T_("Same as --quiet."),
    nullptr
  },

  {
    "verbose", 'v',
    POPT_ARG_NONE, nullptr,
    OPT_VERBOSE,
    T_("Print information on what is being done."),
    nullptr
  },

  {
    "version", 0,
    POPT_ARG_NONE, nullptr,
    OPT_VERSION,
    T_("Print version information and exit."),
    nullptr
  },

  POPT_AUTOHELP
  POPT_TABLEEND
};

IniTeXMFApp::IniTeXMFApp()
{
}

IniTeXMFApp::~IniTeXMFApp()
{
  try
  {
    Finalize(false);
  }
  catch (const exception &)
  {
  }
}

void IniTeXMFApp::Init(int argc, const char* argv[])
{
  bool adminMode = argc > 0 && std::any_of(&argv[1], &argv[argc], [](const char* arg) { return strcmp(arg, "--admin") == 0 || strcmp(arg, "-admin") == 0; });
  Session::InitInfo initInfo(argv[0]);
#if defined(MIKTEX_WINDOWS)
  initInfo.SetOptions({ Session::InitOption::InitializeCOM });
#endif
  initInfo.SetTraceCallback(this);
  session = Session::Create(initInfo);
  packageManager = PackageManager::Create(PackageManager::InitInfo(this));
  FindWizards();
  if (adminMode)
  {
    if (!setupWizardRunning && !session->IsSharedSetup())
    {
      FatalError(T_("Option --admin only makes sense for a shared MiKTeX setup."));
    }
    if (!session->RunningAsAdministrator())
    {
      Warning(T_("Option --admin may require administrative privileges"));
    }
    session->SetAdminMode(true, setupWizardRunning);
  }
  if (session->RunningAsAdministrator() && !session->IsAdminMode())
  {
    Warning(T_("Option --admin should be specified when running this program with administrative privileges"));
  }
  Bootstrap();
  enableInstaller = session->GetConfigValue(MIKTEX_CONFIG_SECTION_MPM, MIKTEX_CONFIG_VALUE_AUTOINSTALL).GetTriState();
  PathName xmlFileName;
  if (session->FindFile("initexmf." MIKTEX_LOG4CXX_CONFIG_FILENAME, MIKTEX_PATH_TEXMF_PLACEHOLDER "/" MIKTEX_PATH_MIKTEX_PLATFORM_CONFIG_DIR, xmlFileName)
    || session->FindFile(MIKTEX_LOG4CXX_CONFIG_FILENAME, MIKTEX_PATH_TEXMF_PLACEHOLDER "/" MIKTEX_PATH_MIKTEX_PLATFORM_CONFIG_DIR, xmlFileName))
  {
    Utils::SetEnvironmentString("MIKTEX_LOG_DIR", GetLogDir().ToString());
    Utils::SetEnvironmentString("MIKTEX_LOG_NAME", GetLogName());
    log4cxx::xml::DOMConfigurator::configure(xmlFileName.ToWideCharString());
  }
  else
  {
    log4cxx::BasicConfigurator::configure();
  }
  isLog4cxxConfigured = true;
  LOG4CXX_INFO(logger, "starting: " << Utils::MakeProgramVersionString(TheNameOfTheGame, MIKTEX_COMPONENT_VERSION_STR));
  FlushPendingTraceMessages();
  if (session->IsAdminMode())
  {
    Verbose(T_("Operating on the shared (system-wide) MiKTeX setup"));
  }
  else
  {
    Verbose(T_("Operating on the private (per-user) MiKTeX setup"));
  }
  PathName myName = PathName(argv[0]).GetFileNameWithoutExtension();
  isMktexlsrMode = myName == "mktexlsr" || myName == "texhash";
  isTexlinksMode = myName == "texlinks";
  session->SetFindFileCallback(this);
}

void IniTeXMFApp::Finalize(bool keepSession)
{
  if (logStream.IsOpen())
  {
    logStream.Close();
  }
  FlushPendingTraceMessages();
  packageInstaller = nullptr;
  packageManager = nullptr;
  if (!keepSession)
  {
    session = nullptr;
  }
}

void IniTeXMFApp::FindWizards()
{
  setupWizardRunning = false;
  updateWizardRunning = false;
  vector<string> invokerNames = Process::GetInvokerNames();
  for (const string& name : invokerNames)
  {
    if (PathName::Compare(name, MIKTEX_PREFIX "update") == 0
      || PathName::Compare(name, MIKTEX_PREFIX "update" MIKTEX_ADMIN_SUFFIX) == 0)
    {
      updateWizardRunning = true;
    }
    else if (
      name.find("basic-miktex") != string::npos ||
      name.find("BASIC-MIKTEX") != string::npos ||
      name.find("setup") != string::npos ||
      name.find("SETUP") != string::npos ||
      name.find("install") != string::npos ||
      name.find("INSTALL") != string::npos)
    {
      setupWizardRunning = true;
    }
  }
}

void IniTeXMFApp::Verbose(const char* lpszFormat, ...)
{
  va_list arglist;
  va_start(arglist, lpszFormat);
  string s;
  try
  {
    s = StringUtil::FormatStringVA(lpszFormat, arglist);
  }
  catch (...)
  {
    va_end(arglist);
    throw;
  }
  va_end(arglist);
  if (!printOnly && isLog4cxxConfigured)
  {
    LOG4CXX_INFO(logger, s);
  }
  if (verbose && !printOnly)
  {
    cout << s << endl;
  }
}

void IniTeXMFApp::PrintOnly(const char* lpszFormat, ...)
{
  if (!printOnly)
  {
    return;
  }
  va_list arglist;
  va_start(arglist, lpszFormat);
  string s;
  try
  {
    s = StringUtil::FormatStringVA(lpszFormat, arglist);
  }
  catch (...)
  {
    va_end(arglist);
    throw;
  }
  va_end(arglist);
  cout << s << endl;
}

void IniTeXMFApp::Warning(const char* lpszFormat, ...)
{
  va_list arglist;
  va_start(arglist, lpszFormat);
  string s;
  try
  {
    s = StringUtil::FormatStringVA(lpszFormat, arglist);
  }
  catch (...)
  {
    va_end(arglist);
    throw;
  }
  va_end(arglist);
  if (isLog4cxxConfigured)
  {
    LOG4CXX_WARN(logger, s);
  }
  if (!quiet)
  {
    cerr << PROGNAME << ": " << T_("warning") << ": " << s << endl;
  }
}

static void Sorry(string reason)
{
  if (cerr.fail())
  {
    return;
  }
  cerr << endl;
  if (reason.empty())
  {
    cerr << StringUtil::FormatString(T_("Sorry, but %s did not succeed."), Q_(TheNameOfTheGame)) << endl;
  }
  else
  {
    cerr
      << StringUtil::FormatString(T_("Sorry, but %s did not succeed for the following reason:"), Q_(TheNameOfTheGame)) << endl << endl
      << "  " << reason << endl;
  }
  log4cxx::RollingFileAppenderPtr appender = log4cxx::Logger::getRootLogger()->getAppender(LOG4CXX_STR("RollingLogFile"));
  if (appender != nullptr)
  {
    cerr
      << endl
      << T_("The log file hopefully contains the information to get MiKTeX going again:") << endl
      << endl
      << "  " << PathName(appender->getFile()).ToUnix() << endl;
  }
  cerr
    << endl
    << T_("You may want to visit the MiKTeX project page, if you need help.") << endl;
}

static void Sorry()
{
  Sorry("");
}

MIKTEXNORETURN void IniTeXMFApp::FatalError(const char* lpszFormat, ...)
{
  va_list arglist;
  va_start(arglist, lpszFormat);
  string s = StringUtil::FormatStringVA(lpszFormat, arglist);
  va_end(arglist);
  if (isLog4cxxConfigured)
  {
    LOG4CXX_FATAL(logger, s);
  }
  else
  {
    cerr << s << endl;
  }
  Sorry(s);
  throw 1;
}

bool IniTeXMFApp::InstallPackage(const string& deploymentName, const PathName& trigger, PathName& installRoot)
{
  if (enableInstaller != TriState::True)
  {
    return false;
  }
  LOG4CXX_INFO(logger, "installing package " << deploymentName << " triggered by " << trigger.ToString());
  Verbose(T_("Installing package %s..."), deploymentName.c_str());
  EnsureInstaller();
  packageInstaller->SetFileLists({ deploymentName }, {});
  packageInstaller->InstallRemove();
  installRoot = session->GetSpecialPath(SpecialPath::InstallRoot);
  return true;
}

bool IniTeXMFApp::TryCreateFile(const MiKTeX::Core::PathName& fileName, MiKTeX::Core::FileType fileType)
{
  return false;
}

bool IniTeXMFApp::ReadDirectory(const PathName& path, vector<string>& subDirNames, vector<string>& fileNames, vector<string>& fileNameInfos)
{
  UNUSED_ALWAYS(path);
  UNUSED_ALWAYS(subDirNames);
  UNUSED_ALWAYS(fileNames);
  UNUSED_ALWAYS(fileNameInfos);
  return false;
}

bool IniTeXMFApp::OnProgress(unsigned level, const PathName& directory)
{
#if 0
  if (verbose && level == 1)
  {
    Verbose(T_("Scanning %s"), Q_(directory));
  }
  else if (level == 1)
  {
    Message(".");
  }
#endif
  return true;
}

void IniTeXMFApp::UpdateFilenameDatabase(const PathName& root)
{
  // unload the file name database
  if (!printOnly && !session->UnloadFilenameDatabase())
  {
    FatalError(T_("The file name database could not be unloaded."));
  }

  unsigned rootIdx = session->DeriveTEXMFRoot(root);

  // remove the old FNDB file
  PathName path = session->GetFilenameDatabasePathName(rootIdx);
  if (File::Exists(path))
  {
    PrintOnly("rm %s", Q_(path));
    if (!printOnly)
    {
      File::Delete(path, { FileDeleteOption::TryHard });
    }
  }

  // create the FNDB file
  PathName fndbPath = session->GetFilenameDatabasePathName(rootIdx);
  if (session->IsCommonRootDirectory(rootIdx))
  {
    Verbose(T_("Creating fndb for common root directory (%s)..."), Q_(root));
  }
  else
  {
    Verbose(T_("Creating fndb for user root directory (%s)..."), Q_(root));
  }
  PrintOnly("fndbcreate %s %s", Q_(fndbPath), Q_(root));
  if (!printOnly)
  {
    Fndb::Create(fndbPath, root, this);
  }
}

void IniTeXMFApp::UpdateFilenameDatabase(unsigned root)
{
  UpdateFilenameDatabase(session->GetRootDirectoryPath(root));
}

void IniTeXMFApp::ListFormats()
{
  for (const FormatInfo& formatInfo : session->GetFormats())
  {
    cout << formatInfo.key << " (" << formatInfo.description << ")" << endl;
  }
}

void IniTeXMFApp::ListMetafontModes()
{
  MIKTEXMFMODE mode;
  for (unsigned i = 0; session->GetMETAFONTMode(i, mode); ++i)
  {
    cout << StringUtil::FormatString("%-8s  %5dx%-5d  %s", mode.mnemonic.c_str(), mode.horizontalResolution, mode.verticalResolution, mode.description.c_str()) << endl;
  }
}

void IniTeXMFApp::Clean()
{
  LinkCategoryOptions linkCategories;
  linkCategories.Set();
  ManageLinks(linkCategories, true, false);
  session->UnloadFilenameDatabase();
  Finalize(true);
  isLog4cxxConfigured = false;
  logger = nullptr;
  Directory::Delete(session->GetSpecialPath(SpecialPath::DataRoot), true);
}

void IniTeXMFApp::RemoveFndb()
{
  session->UnloadFilenameDatabase();
  size_t nRoots = session->GetNumberOfTEXMFRoots();
  for (unsigned r = 0; r < nRoots; ++r)
  {
    PathName path = session->GetFilenameDatabasePathName(r);
    PrintOnly("rm %s", Q_(path));
    if (!printOnly && File::Exists(path))
    {
      Verbose(T_("Removing fndb (%s)..."), Q_(session->GetRootDirectoryPath(r)));
      File::Delete(path, { FileDeleteOption::TryHard });
    }
  }
}

void IniTeXMFApp::SetTeXMFRootDirectories(
#if defined(MIKTEX_WINDOWS)
  bool noRegistry
#endif
  )
{
  Verbose(T_("Registering root directories..."));
  PrintOnly("regroots ur=%s ud=%s uc=%s ui=%s cr=%s cd=%s cc=%s ci=%s", Q_(startupConfig.userRoots), Q_(startupConfig.userDataRoot), Q_(startupConfig.userConfigRoot), Q_(startupConfig.userInstallRoot), Q_(startupConfig.commonRoots), Q_(startupConfig.commonDataRoot), Q_(startupConfig.commonConfigRoot), Q_(startupConfig.commonInstallRoot));
  if (!printOnly)
  {
    RegisterRootDirectoriesOptionSet options;
#if defined(MIKTEX_WINDOWS)
    if (noRegistry)
    {
      options += RegisterRootDirectoriesOption::NoRegistry;
    }
#endif
    session->RegisterRootDirectories(startupConfig, options);
  }
}

void IniTeXMFApp::RunMakeTeX(const string& makeProg, const vector<string>& arguments)
{
  PathName exe;

  if (!session->FindFile(makeProg, FileType::EXE, exe))
  {
    FatalError(T_("The %s executable could not be found."), Q_(makeProg));
  }

  vector<string> xArguments{ makeProg };

  xArguments.insert(xArguments.end(), arguments.begin(), arguments.end());

  if (printOnly)
  {
    xArguments.push_back("--print-only");
  }

  if (verbose)
  {
    xArguments.push_back("--verbose");
  }

  if (quiet)
  {
    xArguments.push_back("--quiet");
  }

  if (session->IsAdminMode())
  {
    xArguments.push_back("--admin");
  }

  switch (enableInstaller)
  {
  case TriState::True:
    xArguments.push_back("--enable-installer");
    break;
  case TriState::False:
    xArguments.push_back("--disable-installer");
    break;
  default:
    break;
  }

  LOG4CXX_INFO(logger, "running: " << CommandLineBuilder(xArguments));
  RunProcess(exe, xArguments);
}

void IniTeXMFApp::MakeFormatFile(const string& formatKey)
{
  if (find(formatsMade.begin(), formatsMade.end(), formatKey) != formatsMade.end())
  {
    return;
  }

  FormatInfo formatInfo;
  if (!session->TryGetFormatInfo(formatKey, formatInfo))
  {
    FatalError(T_("Unknown format: %s"), Q_(formatKey));
  }

  string maker;

  vector<string> arguments;

  if (formatInfo.compiler == "mf")
  {
    maker = MIKTEX_MAKEBASE_EXE;
  }
  else
  {
    maker = MIKTEX_MAKEFMT_EXE;
    arguments.push_back("--engine="s + formatInfo.compiler);
  }

  arguments.push_back("--dest-name="s + formatInfo.name);

  if (!formatInfo.preloaded.empty())
  {
    if (PathName::Compare(formatInfo.preloaded, formatKey) == 0)
    {
      LOG4CXX_FATAL(logger, T_("Rule recursion detected for: ") << formatKey);
      FatalError(T_("Format '%s' cannot be built."), formatKey.c_str());
    }
    // RECURSION
    MakeFormatFile(formatInfo.preloaded);
    arguments.push_back("--preload="s + formatInfo.preloaded);
  }

  if (PathName(formatInfo.inputFile).HasExtension(".ini"))
  {
    arguments.push_back("--no-dump");
  }

  arguments.push_back(formatInfo.inputFile);

  if (!formatInfo.arguments.empty())
  {
    arguments.push_back("--engine-option="s + formatInfo.arguments);
  }

  RunMakeTeX(maker, arguments);

  formatsMade.push_back(formatKey);
}

void IniTeXMFApp::MakeFormatFiles(const vector<string>& formats)
{
  if (formats.empty())
  {
    for (const FormatInfo& formatInfo : session->GetFormats())
    {
      if (!formatInfo.exclude)
      {
        MakeFormatFile(formatInfo.key);
      }
    }
  }
  else
  {
    for (const string& fmt : formats)
    {
      MakeFormatFile(fmt);
    }
  }
}

void IniTeXMFApp::MakeFormatFilesByName(const vector<string>& formatsByName, const string& engine)
{
  for (const string& name : formatsByName)
  {
    bool done = false;
    for (const FormatInfo& formatInfo : session->GetFormats())
    {
      if (PathName::Compare(formatInfo.name, name) == 0 && (engine.empty()
        || (Utils::EqualsIgnoreCase(formatInfo.compiler, engine))))
      {
        MakeFormatFile(formatInfo.key);
        done = true;
      }
    }
    if (!done)
    {
      if (engine.empty())
      {
        FatalError(T_("Unknown format name: %s"), Q_(name));
      }
      else
      {
        FatalError(T_("Unknown format name/engine: %s/%s"), Q_(name), engine.c_str());
      }
    }
  }
}

void IniTeXMFApp::ManageLink(const FileLink& fileLink, bool supportsHardLinks, bool isRemoveRequested, bool allowOverwrite)
{
  LinkType linkType = fileLink.linkType;
  if (linkType == LinkType::Hard && !supportsHardLinks)
  {
    linkType = LinkType::Copy;
  }
  for (const string& linkName : fileLink.linkNames)
  {
    FileExistsOptionSet fileExistsOptions;
#if defined(MIKTEX_UNIX)
    fileExistsOptions += FileExistsOption::SymbolicLink;
#endif
    if (File::Exists(linkName, fileExistsOptions))
    {
      if (!isRemoveRequested && (!allowOverwrite || (linkType == LinkType::Copy && File::Equals(fileLink.target, linkName))))
      {
        continue;
      }
#if defined(MIKTEX_UNIX)
      if (File::IsSymbolicLink(linkName))
      {
        PathName linkTarget = File::ReadSymbolicLink(linkName);
        bool isMiKTeXSymlinked = linkTarget.GetFileName() == PathName(fileLink.target).GetFileName();
        if (!isMiKTeXSymlinked)
        {
          if (File::Exists(linkTarget))
          {
            LOG4CXX_WARN(logger, Q_(linkName) << " already symlinked to " << Q_(linkTarget));
            continue;
          }
          else
          {
            LOG4CXX_TRACE(logger, Q_(linkName) << " is symlinked to non-existing " << Q_(linkTarget));
          }
        }
      }
#endif
      PrintOnly("rm %s", Q_(linkName));
      if (!printOnly)
      {
        File::Delete(linkName, { FileDeleteOption::TryHard, FileDeleteOption::UpdateFndb });
      }
    }
    if (isRemoveRequested)
    {
      continue;
    }
    PathName sourceDirectory(linkName);
    sourceDirectory.RemoveFileSpec();
    if (!Directory::Exists(sourceDirectory) && !printOnly)
    {
      Directory::Create(sourceDirectory);
    }
    switch (linkType)
    {
    case LinkType::Symbolic:
      {
        const char* target = Utils::GetRelativizedPath(fileLink.target.c_str(), sourceDirectory.GetData());
        if (target == nullptr)
        {
          target = fileLink.target.c_str();
        }
        PrintOnly("ln -s %s %s", Q_(linkName), Q_(target));
        if (!printOnly)
        {
          File::CreateLink(target, linkName, { CreateLinkOption::UpdateFndb, CreateLinkOption::Symbolic });
        }
        break;
      }
    case LinkType::Hard:
      PrintOnly("ln %s %s", Q_(fileLink.target), Q_(linkName));
      if (!printOnly)
      {
        File::CreateLink(fileLink.target, linkName, { CreateLinkOption::UpdateFndb });
      }
      break;
    case LinkType::Copy:
      PrintOnly("cp %s %s", Q_(fileLink.target), Q_(linkName));
      if (!printOnly)
      {
        File::Copy(fileLink.target, linkName, { FileCopyOption::UpdateFndb });
      }
      break;
    default:
      MIKTEX_UNEXPECTED();
    }
    if (logStream.IsOpen())
    {
      logStream.WriteLine(linkName);
    }
  }
}

void IniTeXMFApp::RegisterRoots(const vector<PathName>& roots, bool other, bool reg)
{
  string newRoots;

  PathName userInstallRoot;
  PathName userConfigRoot;
  PathName userDataRoot;

  if (!session->IsAdminMode())
  {
    userInstallRoot = session->GetSpecialPath(SpecialPath::UserInstallRoot);
    userConfigRoot = session->GetSpecialPath(SpecialPath::UserConfigRoot);
    userDataRoot = session->GetSpecialPath(SpecialPath::UserDataRoot);
  }

  PathName commonInstallRoot = session->GetSpecialPath(SpecialPath::CommonInstallRoot);
  PathName commonConfigRoot = session->GetSpecialPath(SpecialPath::CommonConfigRoot);
  PathName commonDataRoot = session->GetSpecialPath(SpecialPath::CommonDataRoot);

  for (unsigned r = 0; r < session->GetNumberOfTEXMFRoots(); ++r)
  {
    PathName root = session->GetRootDirectoryPath(r);
    int rootOrdinal = session->DeriveTEXMFRoot(root);
    if (session->IsAdminMode() && !session->IsCommonRootDirectory(rootOrdinal))
    {
      continue;
    }
    if (!session->IsAdminMode()
      && (session->IsCommonRootDirectory(rootOrdinal)
        || root == userInstallRoot
        || root == userConfigRoot
        || root == userDataRoot))
    {
      continue;
    }
    if (root == commonInstallRoot
      || root == commonConfigRoot
      || root == commonDataRoot)
    {
      continue;
    }
    if (!reg)
    {
      bool toBeUnregistered = false;
      for (vector<PathName>::const_iterator it = roots.begin(); it != roots.end() && !toBeUnregistered; ++it)
      {
        if (*it == root)
        {
          toBeUnregistered = true;
        }
      }
      if (toBeUnregistered)
      {
        continue;
      }
    }
    if (!newRoots.empty())
    {
      newRoots += PathName::PathNameDelimiter;
    }
    newRoots += root.GetData();
  }

  if (reg)
  {
    for (const PathName r : roots)
    {
      if (!newRoots.empty())
      {
        newRoots += PathName::PathNameDelimiter;
      }
      newRoots += r.ToString();
    }
  }

  session->RegisterRootDirectories(newRoots, other);

  if (reg)
  {
    for (const PathName& r : roots)
    {
      UpdateFilenameDatabase(r);
    }
  }
}

#if defined(MIKTEX_WINDOWS)

struct ShellFileType {
  const char* lpszComponent;
  const char* lpszExtension;
  const char* lpszUserFriendlyName;
  const char* lpszExecutable;
  int iconIndex;
  bool takeOwnership;
  const char* lpszVerb;
  const char* lpszCommandArgs;
  const char* lpszDdeArgs;
} const shellFileTypes[] = {
  "bib", ".bib", "BibTeX Database", MIKTEX_TEXWORKS_EXE, -2, false, "open", "\"%1\"", nullptr,
  "cls", ".cls", "LaTeX Class", MIKTEX_TEXWORKS_EXE, -2, false, "open", "\"%1\"", nullptr,
  "dtx", ".dtx", "LaTeX Macros", MIKTEX_TEXWORKS_EXE, -2, false, "open", "\"%1\"", nullptr,
  "dvi", ".dvi", "DVI File", MIKTEX_YAP_EXE, 1, false, "open", "/dde", "[open(\"%1\")]",
  "dvi", nullptr, nullptr, MIKTEX_YAP_EXE, INT_MAX, false, "print", "/dde", "[print(\"%1\")]",
  "dvi", nullptr, nullptr, MIKTEX_YAP_EXE, INT_MAX, false, "printto", "/dde", "[printto(\"%1\",\"%2\",\"%3\",\"%4\")]",
  "ltx", ".ltx", "LaTeX Document", MIKTEX_TEXWORKS_EXE, -2, false, "open", "\"%1\"", nullptr,
  "pdf", ".pdf", "PDF File", MIKTEX_TEXWORKS_EXE, INT_MAX, false, "open", "\"%1\"", nullptr,
  "sty", ".sty", "LaTeX Style", MIKTEX_TEXWORKS_EXE, -2, false, "open", "\"%1\"", nullptr,
  "tex", ".tex", "TeX Document", MIKTEX_TEXWORKS_EXE, -2, false, "open", "\"%1\"", nullptr,
};

void IniTeXMFApp::RegisterShellFileTypes(bool reg)
{
  for (const ShellFileType& sft : shellFileTypes)
  {
    string progId = Utils::MakeProgId(sft.lpszComponent);
    if (reg)
    {
      PathName exe;
      if (sft.lpszExecutable != nullptr && !session->FindFile(sft.lpszExecutable, FileType::EXE, exe))
      {
        FatalError(T_("Could not find %s."), sft.lpszExecutable);
      }
      string command;
      if (sft.lpszExecutable != nullptr && sft.lpszCommandArgs != nullptr)
      {
        command = '\"';
        command += exe.GetData();
        command += "\" ";
        command += sft.lpszCommandArgs;
      }
      string iconPath;
      if (sft.lpszExecutable != 0 && sft.iconIndex != INT_MAX)
      {
        iconPath += exe.GetData();
        iconPath += ",";
        iconPath += std::to_string(sft.iconIndex);
      }
      if (sft.lpszUserFriendlyName != 0 || !iconPath.empty())
      {
        Utils::RegisterShellFileType(progId, sft.lpszUserFriendlyName, iconPath);
      }
      if (sft.lpszVerb != nullptr && (!command.empty() || sft.lpszDdeArgs != nullptr))
      {
        Utils::RegisterShellVerb(progId, sft.lpszVerb, command, sft.lpszDdeArgs == nullptr ? "" : sft.lpszDdeArgs);
      }
      if (sft.lpszExtension != nullptr)
      {
        Utils::RegisterShellFileAssoc(sft.lpszExtension, progId, sft.takeOwnership);
      }
    }
    else
    {
      Utils::UnregisterShellFileType(progId);
      if (sft.lpszExtension != nullptr)
      {
        Utils::UnregisterShellFileAssoc(sft.lpszExtension, progId);
      }
    }
  }
}
#endif

void IniTeXMFApp::ModifyPath()
{
#if defined(MIKTEX_WINDOWS)
  Utils::CheckPath(true);
#else
  // TODO: check path
  UNIMPLEMENTED();
#endif
}

// TODO: extra source file;
// TODO: better: configuration file (miktex-links.ini)
vector<FileLink> miktexFileLinks =
{
#if defined(MIKTEX_WINDOWS)
  { "arctrl" MIKTEX_EXE_FILE_SUFFIX, { "pdfclose", "pdfdde", "pdfopen" } },
#endif
  { "cjklatex" MIKTEX_EXE_FILE_SUFFIX, { "bg5pluslatex", "bg5pluspdflatex", "bg5latex", "bg5pdflatex", "bg5platex", "bg5ppdflatex", "cef5latex", "cef5pdflatex", "ceflatex", "cefpdflatex", "cefslatex", "cefspdflatex", "gbklatex", "gbkpdflatex", "sjislatex", "sjispdflatex" } },

  { MIKTEX_AFM2TFM_EXE, { "afm2tfm" } },
#if defined(MIKTEX_WINDOWS)
  { MIKTEX_ASY_EXE, { "asy" } },
#endif
  { MIKTEX_AUTOSP_EXE, { "autosp" } },
  { MIKTEX_AXOHELP_EXE,{ "axohelp" } },
  { MIKTEX_BG5CONV_EXE, { "bg5conv" } },
  { MIKTEX_BIBTEX8_EXE, { "bibtex8" } },
  { MIKTEX_BIBTEXU_EXE, { "bibtexu" } },
  { MIKTEX_BIBTEX_EXE, { "bibtex" } },
  { MIKTEX_CEF5CONV_EXE, { "cef5conv" } },
  { MIKTEX_CEFCONV_EXE, { "cefconv" } },
  { MIKTEX_CEFSCONV_EXE, { "cefsconv" } },
  { MIKTEX_CHKTEX_EXE, { "chktex" } },
  { MIKTEX_CTANGLE_EXE,{ "ctangle" } },
  { MIKTEX_CWEAVE_EXE,{ "cweave" } },
  { MIKTEX_DEVNAG_EXE, { "devnag" } },
  { MIKTEX_DVICOPY_EXE, { "dvicopy" } },
  { MIKTEX_DVIPDFMX_EXE, { "dvipdfm", "dvipdfmx", "ebb", "extractbb", "xbb", "xdvipdfmx", MIKTEX_XDVIPDFMX_EXE } },
  { MIKTEX_DVIPDFT_EXE,{ "dvipdft" } },
  { MIKTEX_DVIPNG_EXE, { "dvipng" } },
  { MIKTEX_DVIPS_EXE, { "dvips" } },
  { MIKTEX_DVISVGM_EXE, { "dvisvgm" } },
  { MIKTEX_DVITYPE_EXE, { "dvitype" } },
  { MIKTEX_EPSFFIT_EXE, { "epsffit" } },
  { MIKTEX_EPSTOPDF_EXE,{ "epstopdf" } },
  { MIKTEX_EXTCONV_EXE, { "extconv" } },
  { MIKTEX_FRIBIDIXETEX_EXE, { "fribidixetex" } },
  { MIKTEX_GFTODVI_EXE, { "gftodvi" } },
  { MIKTEX_GFTOPK_EXE, { "gftopk" } },
  { MIKTEX_GFTYPE_EXE, { "gftype" } },
  { MIKTEX_GREGORIO_EXE, { "gregorio" } },
  { MIKTEX_HBF2GF_EXE, { "hbf2gf" } },
  { MIKTEX_LACHECK_EXE, { "lacheck" } },
  { MIKTEX_MAKEBASE_EXE, { "makebase" } },
  { MIKTEX_MAKEFMT_EXE, { "makefmt" } },
  { MIKTEX_MAKEINDEX_EXE, { "makeindex" } },
  { MIKTEX_MAKEPK_EXE, { "makepk" } },
  { MIKTEX_MAKETFM_EXE, { "maketfm" } },
  { MIKTEX_MFT_EXE, { "mft" } },
  { MIKTEX_MF_EXE, { "mf", "inimf", "virmf" } },
  { MIKTEX_MKOCP_EXE, { "mkocp" } },
  { MIKTEX_MPOST_EXE, { "dvitomp", "mpost" } },
  { MIKTEX_ODVICOPY_EXE, { "odvicopy" } },
  { MIKTEX_OFM2OPL_EXE, { "ofm2opl" } },
  { MIKTEX_OMEGA_EXE, { "omega" } },
  { MIKTEX_OPL2OFM_EXE, { "opl2ofm" } },
  { MIKTEX_OTP2OCP_EXE, { "otp2ocp" } },
  { MIKTEX_OUTOCP_EXE, { "outocp" } },
  { MIKTEX_OVF2OVP_EXE, { "ovf2ovp" } },
  { MIKTEX_OVP2OVF_EXE, { "ovp2ovf" } },
  { MIKTEX_PDFTEX_EXE, { "pdftex", MIKTEX_LATEX_EXE, MIKTEX_PDFLATEX_EXE } },
  { MIKTEX_PDFTOSRC_EXE, { "pdftosrc" } },
  { MIKTEX_PK2BM_EXE, { "pk2bm" } },
  { MIKTEX_PLTOTF_EXE, { "pltotf" } },
  { MIKTEX_PMXAB_EXE, { "pmxab" } },
  { MIKTEX_POOLTYPE_EXE, { "pooltype" } },
  { MIKTEX_PREPMX_EXE, { "prepmx" } },
  { MIKTEX_PS2PK_EXE, { "ps2pk" } },
  { MIKTEX_PSBOOK_EXE, { "psbook" } },
  { MIKTEX_PSNUP_EXE, { "psnup" } },
  { MIKTEX_PSRESIZE_EXE, { "psresize" } },
  { MIKTEX_PSSELECT_EXE, { "psselect" } },
  { MIKTEX_PSTOPS_EXE, { "pstops" } },
  { MIKTEX_REBAR_EXE, { "rebar" } },
  { MIKTEX_SCOR2PRT_EXE, { "scor2prt" } },
  { MIKTEX_SJISCONV_EXE, { "sjisconv" } },
  { MIKTEX_T4HT_EXE, { "t4ht" } },
  { MIKTEX_TANGLE_EXE, { "tangle" } },
  { MIKTEX_TEX4HT_EXE, { "tex4ht" } },
#if defined(MIKTEX_QT)
  { MIKTEX_TEXWORKS_EXE, { "texworks" } },
#endif
  { MIKTEX_TEX_EXE, { "tex", "initex", "virtex" } },
  { MIKTEX_TFTOPL_EXE, { "tftopl" } },
  { MIKTEX_TTF2AFM_EXE, { "ttf2afm" } },
  { MIKTEX_TTF2PK_EXE, { "ttf2pk" } },
  { MIKTEX_TTF2TFM_EXE, { "ttf2tfm" } },
  { MIKTEX_VFTOVP_EXE, { "vftovp" } },
  { MIKTEX_VPTOVF_EXE, { "vptovf" } },
  { MIKTEX_WEAVE_EXE, { "weave" } },
  { MIKTEX_XETEX_EXE, { "xetex", MIKTEX_XELATEX_EXE } },
#if defined(WITH_KPSEWHICH)
  { MIKTEX_KPSEWHICH_EXE, { "kpsewhich" } },
#endif
#if defined(MIKTEX_MACOS_BUNDLE)
  { MIKTEX_INITEXMF_EXE, { MIKTEX_INITEXMF_EXE }},
  { MIKTEX_MPM_EXE, { MIKTEX_MPM_EXE } },
  { "mthelp" MIKTEX_EXE_FILE_SUFFIX, { "mthelp" MIKTEX_EXE_FILE_SUFFIX } },
#endif
#if defined(WITH_MKTEXLSR)
  { MIKTEX_INITEXMF_EXE, { "mktexlsr" }, LinkType::Copy },
#endif
#if defined(WITH_TEXHASH)
  { MIKTEX_INITEXMF_EXE, { "texhash" }, LinkType::Copy },
#endif
#if defined(WITH_TEXLINKS)
  { MIKTEX_INITEXMF_EXE, { "texlinks" }, LinkType::Copy },
#endif
#if defined(WITH_UPDMAP)
  { "mkfntmap" MIKTEX_EXE_FILE_SUFFIX, { "updmap" } },
#endif
#if defined(WITH_TEXDOC)
  { "mthelp" MIKTEX_EXE_FILE_SUFFIX, { "texdoc" } },
#endif
#if defined(WITH_POPPLER_UTILS)
  { MIKTEX_PDFDETACH_EXE, { "pdfdetach" } },
  { MIKTEX_PDFFONTS_EXE, { "pdffonts" } },
  { MIKTEX_PDFIMAGES_EXE, { "pdfimages" } },
  { MIKTEX_PDFINFO_EXE, { "pdfinfo" } },
  { MIKTEX_PDFSEPARATE_EXE, { "pdfseparate" } },
#if 0
  { MIKTEX_PDFSIG_EXE, { "pdfsig" } },
#endif
  { MIKTEX_PDFTOCAIRO_EXE,{ "pdftocairo" } },
  { MIKTEX_PDFTOHTML_EXE, { "pdftohtml" } },
  { MIKTEX_PDFTOPPM_EXE, { "pdftoppm" } },
  { MIKTEX_PDFTOPS_EXE, { "pdftops" } },
  { MIKTEX_PDFTOTEXT_EXE, { "pdftotext" } },
  { MIKTEX_PDFUNITE_EXE, { "pdfunite" } },
#endif
#if defined(MIKTEX_WINDOWS)
  { MIKTEX_CONSOLE_EXE, { MIKTEX_TASKBAR_ICON_EXE, MIKTEX_UPDATE_EXE } },
  { MIKTEX_CONSOLE_ADMIN_EXE,{ MIKTEX_UPDATE_ADMIN_EXE } },
#endif
};

vector<FileLink> lua52texLinks =
{
  { MIKTEX_LUATEX_EXE, { MIKTEX_PREFIX "texlua", MIKTEX_PREFIX "texluac", "luatex", "texlua", "texluac", MIKTEX_LUALATEX_EXE } },
};

#if defined(WITH_LUA53TEX)
vector<FileLink> lua53texLinks =
{
  { MIKTEX_LUA53TEX_EXE, { MIKTEX_PREFIX "texlua", MIKTEX_PREFIX "texluac", "luatex", "texlua", "texluac", MIKTEX_LUALATEX_EXE } },
};
#endif

vector<FileLink> IniTeXMFApp::CollectLinks(LinkCategoryOptions linkCategories)
{
  vector<FileLink> result;
  PathName pathLocalBinDir = session->GetSpecialPath(SpecialPath::LocalBinDirectory);
  PathName pathBinDir = session->GetSpecialPath(SpecialPath::BinDirectory);

#if defined(WITH_LUA53TEX)
  bool useLua53 = false;
  string luaver;
  if (session->TryGetConfigValue("luatex", "luaver", luaver))
  {
    if (luaver != "5.2" && luaver != "5.3")
    {
      MIKTEX_FATAL_ERROR_2(T_("Invalid configuration value."), "name", "luaver", "value", luaver);
    }
    useLua53 = luaver == "5.3";
  }
#endif

  if (linkCategories[LinkCategory::MiKTeX])
  {
    vector<FileLink> links = miktexFileLinks;
#if defined(WITH_LUA53TEX)
    if (useLua53)
    {
      links.insert(links.end(), lua53texLinks.begin(), lua53texLinks.end());
    }
    else
    {
      links.insert(links.end(), lua52texLinks.begin(), lua52texLinks.end());
    }
#else
    links.insert(links.end(), lua52texLinks.begin(), lua52texLinks.end());
#endif
#if defined(MIKTEX_MACOS_BUNDLE)
    PathName console(session->GetSpecialPath(SpecialPath::MacOsDirectory) / MIKTEX_MACOS_BUNDLE_NAME);
    links.push_back(FileLink(console.ToString(), { MIKTEX_CONSOLE_EXE }, LinkType::Symbolic));
#endif
    for (const FileLink& fileLink : links)
    {
      PathName targetPath;
      if (Utils::IsAbsolutePath(fileLink.target))
      {
        targetPath = fileLink.target;
      }
      else
      {
        targetPath = pathBinDir / fileLink.target;
      }
      string extension = targetPath.GetExtension();
      if (File::Exists(targetPath))
      {
        vector<string> linkNames;
        for (const string& linkName : fileLink.linkNames)
        {
          PathName linkPath = pathLocalBinDir / linkName;
          if (linkPath == targetPath)
          {
            continue;
          }
          if (!extension.empty())
          {
            linkPath.AppendExtension(extension);
          }
          linkNames.push_back(linkPath.ToString());
        }
        result.push_back(FileLink(targetPath.ToString(), linkNames, fileLink.linkType));
      }
      else
      {
        Warning(T_("The link target %s does not exist."), Q_(targetPath));
      }
    }
  }

  if (linkCategories[LinkCategory::Formats])
  {
    for (const FormatInfo& formatInfo : session->GetFormats())
    {
      if (formatInfo.noExecutable)
      {
        continue;
      }
      string engine = formatInfo.compiler;
#if defined(WITH_LUA53TEX)
      if (engine == "luatex" && useLua53)
      {
        engine = "lua53tex";
      }
#endif
      PathName enginePath;
      if (!session->FindFile(string(MIKTEX_PREFIX) + engine, FileType::EXE, enginePath))
      {
        Warning(T_("The %s executable could not be found."), engine.c_str());
        continue;
      }
      PathName exePath(pathLocalBinDir, formatInfo.name);
      if (strlen(MIKTEX_EXE_FILE_SUFFIX) > 0)
      {
        exePath.AppendExtension(MIKTEX_EXE_FILE_SUFFIX);
      }
      if (!(enginePath == exePath))
      {
        result.push_back(FileLink(enginePath.ToString(), { exePath.ToString() }));
      }
    }
  }

  if (linkCategories[LinkCategory::Scripts])
  {

    PathName scriptsIni;
    if (!session->FindFile(MIKTEX_PATH_SCRIPTS_INI, MIKTEX_PATH_TEXMF_PLACEHOLDER, scriptsIni))
    {
      FatalError(T_("Script configuration file not found."));
    }
    unique_ptr<Cfg> config(Cfg::Create());
    config->Read(scriptsIni, true);
    for (const shared_ptr<Cfg::Key>& key : config->GetKeys())
    {
      PathName wrapper = session->GetSpecialPath(SpecialPath::InternalBinDirectory);
      wrapper.AppendDirectoryDelimiter();
      wrapper.Append("run", false);
      wrapper.Append(key->GetName(), false);
#if defined(WITH_LUA53TEX)
      if (useLua53 && key->GetName() == "texlua")
      {
        wrapper.Append("53", false);
      }
#endif
      wrapper.Append(MIKTEX_EXE_FILE_SUFFIX, false);
      if (!File::Exists(wrapper))
      {
        continue;
      }
      for (const shared_ptr<Cfg::Value>& v : key->GetValues())
      {
        PathName pathExe(pathLocalBinDir, v->GetName());
        if (strlen(MIKTEX_EXE_FILE_SUFFIX) > 0)
        {
          pathExe.AppendExtension(MIKTEX_EXE_FILE_SUFFIX);
        }
        result.push_back(FileLink(wrapper.ToString(), { pathExe.ToString() }));
      }
    }
  }

  return result;
}

void IniTeXMFApp::ManageLinks(LinkCategoryOptions linkCategories, bool remove, bool force)
{
  PathName pathBinDir = session->GetSpecialPath(SpecialPath::BinDirectory);
  PathName internalBinDir = session->GetSpecialPath(SpecialPath::InternalBinDirectory);

  // TODO: MIKTEX_ASSERT(pathBinDir.GetMountPoint() == internalBinDir.GetMountPoint());

  bool supportsHardLinks = Utils::SupportsHardLinks(pathBinDir);

  if (!remove && !Directory::Exists(pathBinDir))
  {
    Directory::Create(pathBinDir);
  }

  if (!remove && logStream.IsOpen())
  {
    logStream.WriteLine("[files]");
  }

  for (const FileLink& fileLink : CollectLinks(linkCategories))
  {
    ManageLink(fileLink, supportsHardLinks, remove, force);
  }
}

#if defined(MIKTEX_UNIX)
void IniTeXMFApp::MakeScriptsExecutable()
{
  PathName scriptsIni;
  if (!session->FindFile(MIKTEX_PATH_SCRIPTS_INI, MIKTEX_PATH_TEXMF_PLACEHOLDER, scriptsIni))
  {
    FatalError(T_("Script configuration file not found."));
  }
  unique_ptr<Cfg> config(Cfg::Create());
  config->Read(scriptsIni, true);
  AutoRestore<TriState> x(enableInstaller);
  enableInstaller = TriState::False;
  for (const shared_ptr<Cfg::Key>& key : config->GetKeys())
  {
    if (key->GetName() != "sh")
    {
      continue;
    }
    for (const shared_ptr<Cfg::Value>& val : key->GetValues())
    {
      PathName scriptPath;
      if (!session->FindFile(val->GetValue(), MIKTEX_PATH_TEXMF_PLACEHOLDER, scriptPath))
      {
        continue;
      }
      File::SetAttributes(scriptPath, File::GetAttributes(scriptPath) + FileAttribute::Executable);
    }
  }
}
#endif

void IniTeXMFApp::MakeLanguageDat(bool force)
{
  Verbose(T_("Creating language.dat, language.dat.lua and language.def..."));

  if (printOnly)
  {
    return;
  }

  PathName dir;

  PathName languageDatPath = session->GetSpecialPath(SpecialPath::ConfigRoot);
  languageDatPath /= MIKTEX_PATH_LANGUAGE_DAT;
  dir = languageDatPath;
  dir.RemoveFileSpec();
  Directory::Create(dir);
  StreamWriter languageDat(languageDatPath);

  PathName languageDatLuaPath = session->GetSpecialPath(SpecialPath::ConfigRoot);
  languageDatLuaPath /= MIKTEX_PATH_LANGUAGE_DAT_LUA;
  dir = languageDatLuaPath;
  dir.RemoveFileSpec();
  Directory::Create(dir);
  StreamWriter languageDatLua(languageDatLuaPath);

  PathName languageDefPath = session->GetSpecialPath(SpecialPath::ConfigRoot);
  languageDefPath /= MIKTEX_PATH_LANGUAGE_DEF;
  dir = languageDefPath;
  dir.RemoveFileSpec();
  Directory::Create(dir);
  StreamWriter languageDef(languageDefPath);

  languageDatLua.WriteLine("return {");
  languageDef.WriteLine("%% e-TeX V2.2");

  for (const LanguageInfo& languageInfo : session->GetLanguages())
  {
    if (languageInfo.exclude)
    {
      continue;
    }

    PathName loaderPath;
    if (!session->FindFile(languageInfo.loader, "%r/tex//", loaderPath))
    {
      continue;
    }

    // language.dat
    languageDat.WriteFormattedLine("%s %s", languageInfo.key.c_str(), languageInfo.loader.c_str());
    for (const string& synonym : StringUtil::Split(languageInfo.synonyms, ','))
    {
      languageDat.WriteFormattedLine("=%s", synonym.c_str());
    }

    // language.def
    languageDef.WriteFormattedLine("\\addlanguage{%s}{%s}{}{%d}{%d}", languageInfo.key.c_str(), languageInfo.loader.c_str(), languageInfo.lefthyphenmin, languageInfo.righthyphenmin);

    // language.dat.lua
    languageDatLua.WriteFormattedLine("\t['%s'] = {", languageInfo.key.c_str());
    languageDatLua.WriteFormattedLine("\t\tloader='%s',", languageInfo.loader.c_str());
    languageDatLua.WriteFormattedLine("\t\tlefthyphenmin=%d,", languageInfo.lefthyphenmin);
    languageDatLua.WriteFormattedLine("\t\trighthyphenmin=%d,", languageInfo.righthyphenmin);
    languageDatLua.Write("\t\tsynonyms={ ");
    int nSyn = 0;
    for (const string& synonym : StringUtil::Split(languageInfo.synonyms, ','))
    {
      languageDatLua.WriteFormatted("%s'%s'", nSyn > 0 ? "," : "", synonym.c_str());
      nSyn++;
    }
    languageDatLua.WriteLine(" },");
    languageDatLua.WriteFormattedLine("\t\tpatterns='%s',", languageInfo.patterns.c_str());
    languageDatLua.WriteFormattedLine("\t\thyphenation='%s',", languageInfo.hyphenation.c_str());
    if (!languageInfo.luaspecial.empty())
    {
      languageDatLua.WriteFormattedLine("\t\tspecial='%s',", languageInfo.luaspecial.c_str());
    }
    languageDatLua.WriteLine("\t},");
  }

  languageDatLua.WriteLine("}");

  languageDatLua.Close();
  Fndb::Add(languageDatLuaPath);

  languageDef.Close();
  Fndb::Add(languageDefPath);

  languageDat.Close();
  Fndb::Add(languageDatPath);
}

void IniTeXMFApp::MakeMaps(bool force)
{
  PathName pathMkfontmap;
  if (!session->FindFile("mkfntmap", FileType::EXE, pathMkfontmap))
  {
    FatalError(T_("The mkfntmap executable could not be found."));
  }
  vector<string> arguments{ "mkfntmap" };
  if (verbose)
  {
    arguments.push_back("--verbose");
  }
  if (session->IsAdminMode())
  {
    arguments.push_back("--admin");
  }
  if (force)
  {
    arguments.push_back("--force");
  }
  switch (enableInstaller)
  {
  case TriState::True:
    arguments.push_back("--enable-installer");
    break;
  case TriState::False:
    arguments.push_back("--disable-installer");
    break;
  default:
    break;
  }
  if (printOnly)
  {
    PrintOnly("%s", CommandLineBuilder(arguments).ToString().c_str());
  }
  else
  {
    LOG4CXX_INFO(logger, "running: " << CommandLineBuilder(arguments));
    RunProcess(pathMkfontmap, arguments);
  }
}

void IniTeXMFApp::CreateConfigFile(const string& relPath, bool edit)
{
  PathName configFile(session->GetSpecialPath(SpecialPath::ConfigRoot));
  bool haveConfigFile = false;
  for (const auto& shortCut : configShortcuts)
  {
    if (PathName::Compare(relPath, shortCut.lpszShortcut) == 0)
    {
      configFile /= shortCut.lpszFile;
      haveConfigFile = true;
      break;
    }
  }
  if (!haveConfigFile)
  {
    PathName fileName(relPath);
    fileName.RemoveDirectorySpec();
    if (fileName == relPath)
    {
      configFile /= MIKTEX_PATH_MIKTEX_CONFIG_DIR;
    }
    configFile /= relPath;
    configFile.SetExtension(".ini", false);
    haveConfigFile = true;
  }
  if (!File::Exists(configFile))
  {
    if (!session->TryCreateFromTemplate(configFile))
    {
      Directory::Create(PathName(configFile).RemoveFileSpec());
      StreamWriter writer(configFile);
      writer.Close();
      Fndb::Add(configFile);
    }
  }
  if (edit)
  {
    string editor;
    const char* lpszEditor = getenv("EDITOR");
    if (lpszEditor != nullptr)
    {
      editor = lpszEditor;
    }
    else
    {
#if defined(MIKTEX_WINDOWS)
      editor = "notepad.exe";
#else
      FatalError(T_("Environment variable EDITOR is not defined."));
#endif
    }
    Process::Start(editor, vector<string>{ editor, configFile.ToString() });
  }
}

void IniTeXMFApp::SetConfigValue(const string& valueSpec)
{
  const char* lpsz = valueSpec.c_str();
  string section;
  bool haveSection = (*lpsz == '[');
  if (haveSection)
  {
    ++lpsz;
    for (; *lpsz != 0 && *lpsz != ']'; ++lpsz)
    {
      section += *lpsz;
    }
    if (*lpsz == 0)
    {
      LOG4CXX_FATAL(logger, T_("Invalid value: ") << Q_(valueSpec));
      FatalError(T_("The configuration value '%s' could not be set."), Q_(valueSpec));
    }
    ++lpsz;
  }
  string valueName;
  for (; *lpsz != 0 && *lpsz != '='; ++lpsz)
  {
    valueName += *lpsz;
  }
  if (*lpsz == 0)
  {
    LOG4CXX_FATAL(logger, T_("Invalid value: ") << Q_(valueSpec));
    FatalError(T_("The configuration value '%s' could not be set."), Q_(valueSpec));
  }
  ++lpsz;
  string value = lpsz;
  session->SetConfigValue(section, valueName, value);
}

void IniTeXMFApp::ShowConfigValue(const string& valueSpec)
{
  const char* lpsz = valueSpec.c_str();
  string section;
  bool haveSection = (*lpsz == '[');
  if (haveSection)
  {
    ++lpsz;
    for (; *lpsz != 0 && *lpsz != ']'; ++lpsz)
    {
      section += *lpsz;
    }
    if (*lpsz == 0)
    {
      FatalError(T_("Invalid value: %s."), Q_(valueSpec));
    }
    ++lpsz;
  }
  string valueName = lpsz;
  string value;
  if (session->TryGetConfigValue(section, valueName, value))
  {
    cout << value << endl;
  }
}

void IniTeXMFApp::ReportMiKTeXVersion()
{
  vector<string> invokerNames = Process::GetInvokerNames();
  if (xml)
  {
    xmlWriter.StartElement("setup");
    xmlWriter.StartElement("version");
    xmlWriter.Text(Utils::GetMiKTeXVersionString());
    xmlWriter.EndElement();
    xmlWriter.StartElement("sharedsetup");
    xmlWriter.AddAttribute("value", (session->IsSharedSetup() ? "true" : "false"));
    xmlWriter.EndElement();
#if defined(MIKTEX_WINDOWS)
    xmlWriter.StartElement("systemadmin");
    xmlWriter.AddAttribute("value", (session->IsUserAnAdministrator() ? "true" : "false"));
    xmlWriter.EndElement();
    xmlWriter.StartElement("poweruser");
    xmlWriter.AddAttribute("value", (session->IsUserAPowerUser() ? "true" : "false"));
    xmlWriter.EndElement();
    xmlWriter.EndElement();
#endif
  }
  else
  {
    cout << "MiKTeX: " << Utils::GetMiKTeXBannerString() << endl;
    cout << T_("Invokers:") << " ";
    bool first = true;
    for (const string& name : invokerNames)
    {
      if (!first)
      {
        cout << "/";
      }
      first = false;
      cout << name;
    }
    cout << endl;
    cout << "SharedSetup: " << (session->IsSharedSetup() ? T_("yes") : T_("no")) << endl;
    cout << "SystemAdmin: " << (session->IsUserAnAdministrator() ? T_("yes") : T_("no")) << endl;
    cout << "RootPrivileges: " << (session->RunningAsAdministrator() ? T_("yes") : T_("no")) << endl;     
#if defined(MIKTEX_WINDOWS)
    cout << "PowerUser: " << (session->IsUserAPowerUser() ? T_("yes") : T_("no")) << endl;
#endif
  }
}

void IniTeXMFApp::ReportOSVersion()
{
  if (xml)
  {
    xmlWriter.StartElement("os");
    xmlWriter.StartElement("version");
    xmlWriter.Text(Utils::GetOSVersionString());
    xmlWriter.EndElement();
    xmlWriter.EndElement();
  }
  else
  {
    cout << "OS: " << Utils::GetOSVersionString() << endl;
  }
}

void IniTeXMFApp::ReportRoots()
{
  if (xml)
  {
    xmlWriter.StartElement("roots");
    for (unsigned idx = 0; idx < session->GetNumberOfTEXMFRoots(); ++idx)
    {
      xmlWriter.StartElement("path");
      PathName root = session->GetRootDirectoryPath(idx);
      xmlWriter.AddAttribute("index", std::to_string(idx));
      if (root == session->GetSpecialPath(SpecialPath::UserInstallRoot))
      {
        xmlWriter.AddAttribute("userinstall", "true");
      }
      if (root == session->GetSpecialPath(SpecialPath::UserDataRoot))
      {
        xmlWriter.AddAttribute("userdata", "true");
      }
      if (root == session->GetSpecialPath(SpecialPath::UserConfigRoot))
      {
        xmlWriter.AddAttribute("userconfig", "true");
      }
      if (root == session->GetSpecialPath(SpecialPath::CommonInstallRoot))
      {
        xmlWriter.AddAttribute("commoninstall", "true");
      }
      if (root == session->GetSpecialPath(SpecialPath::CommonDataRoot))
      {
        xmlWriter.AddAttribute("commondata", "true");
      }
      if (root == session->GetSpecialPath(SpecialPath::CommonConfigRoot))
      {
        xmlWriter.AddAttribute("commonconfig", "true");
      }
      xmlWriter.Text(root.GetData());
      xmlWriter.EndElement();
    }
    xmlWriter.EndElement();
  }
  else
  {
    for (unsigned idx = 0; idx < session->GetNumberOfTEXMFRoots(); ++idx)
    {
      cout << StringUtil::FormatString(T_("Root #%u"), idx) << ": " << session->GetRootDirectoryPath(idx) << endl;
    }
    cout << "UserInstall: " << session->GetSpecialPath(SpecialPath::UserInstallRoot) << endl;
    cout << "UserData: " << session->GetSpecialPath(SpecialPath::UserDataRoot) << endl;
    cout << "UserConfig: " << session->GetSpecialPath(SpecialPath::UserConfigRoot) << endl;
    cout << "CommonInstall: " << session->GetSpecialPath(SpecialPath::CommonInstallRoot) << endl;
    cout << "CommonData: " << session->GetSpecialPath(SpecialPath::CommonDataRoot) << endl;
    cout << "CommonConfig: " << session->GetSpecialPath(SpecialPath::CommonConfigRoot) << endl;
  }
}

void IniTeXMFApp::ReportFndbFiles()
{
  if (xml)
  {
    xmlWriter.StartElement("fndb");
    for (unsigned idx = 0; idx < session->GetNumberOfTEXMFRoots(); ++idx)
    {
      PathName absFileName;
      if (session->FindFilenameDatabase(idx, absFileName))
      {
        xmlWriter.StartElement("path");
        xmlWriter.AddAttribute("index", std::to_string(idx));
        xmlWriter.Text(absFileName.GetData());
        xmlWriter.EndElement();
      }
    }
    unsigned r = session->DeriveTEXMFRoot(session->GetMpmRootPath());
    PathName path;
    if (session->FindFilenameDatabase(r, path))
    {
      xmlWriter.StartElement("mpmpath");
      xmlWriter.Text(path.GetData());
      xmlWriter.EndElement();
    }
    xmlWriter.EndElement();
  }
  else
  {
    for (unsigned idx = 0; idx < session->GetNumberOfTEXMFRoots(); ++idx)
    {
      PathName absFileName;
      cout << "fndb #" << idx << ": ";
      if (session->FindFilenameDatabase(idx, absFileName))
      {
        cout << absFileName << endl;
      }
      else
      {
        cout << T_("<does not exist>") << endl;
      }
    }
    unsigned r = session->DeriveTEXMFRoot(session->GetMpmRootPath());
    PathName path;
    cout << "fndbmpm: ";
    if (session->FindFilenameDatabase(r, path))
    {
      cout << path << endl;
    }
    else
    {
      cout << T_("<does not exist>") << endl;
    }
  }
}

#if defined(MIKTEX_WINDOWS)
void IniTeXMFApp::ReportEnvironmentVariables()
{
  wchar_t* lpszEnv = reinterpret_cast<wchar_t*>(GetEnvironmentStringsW());
  if (lpszEnv == nullptr)
  {
    return;
  }
  xmlWriter.StartElement("environment");
  for (wchar_t* p = lpszEnv; *p != 0; p += wcslen(p) + 1)
  {
    Tokenizer tok(StringUtil::WideCharToUTF8(p), "=");
    if (!tok)
    {
      continue;
    }
    xmlWriter.StartElement("env");
    xmlWriter.AddAttribute("name", *tok);
    ++tok;
    if (tok)
    {
      xmlWriter.Text(*tok);
    }
    xmlWriter.EndElement();
  }
  xmlWriter.EndElement();
  FreeEnvironmentStringsW(lpszEnv);
}
#endif

void IniTeXMFApp::ReportBrokenPackages()
{
  vector<string> broken;
  unique_ptr<PackageIterator> pkgIter(packageManager->CreateIterator());
  PackageInfo packageInfo;
  for (int idx = 0; pkgIter->GetNext(packageInfo); ++idx)
  {
    if (!packageInfo.IsPureContainer()
      && packageInfo.IsInstalled()
      && packageInfo.deploymentName.compare(0, 7, "miktex-") == 0)
    {
      if (!(packageManager->TryVerifyInstalledPackage(packageInfo.deploymentName)))
      {
        broken.push_back(packageInfo.deploymentName);
      }
    }
  }
  pkgIter->Dispose();
  if (!broken.empty())
  {
    if (xml)
    {
      xmlWriter.StartElement("packages");
      for (const string& name : broken)
      {
        xmlWriter.StartElement("package");
        xmlWriter.AddAttribute("name", name);
        xmlWriter.AddAttribute("integrity", "damaged");
        xmlWriter.EndElement();
      }
      xmlWriter.EndElement();
    }
    else
    {
      for (const string& name : broken)
      {
        cout << name << ": " << T_("needs to be reinstalled") << endl;
      }
    }
  }
}

void IniTeXMFApp::ReportLine(const string& str)
{
  Verbose("%s", str.c_str());
}

bool IniTeXMFApp::OnRetryableError(const string& message)
{
  return false;
}

bool IniTeXMFApp::OnProgress(Notification nf)
{
  UNUSED_ALWAYS(nf);
  return true;
}

void IniTeXMFApp::Bootstrap()
{
#if defined(WITH_BOOTSTRAPPING)
  vector<string> neededPackages;
  for (const string& package : StringUtil::Split(MIKTEX_BOOTSTRAPPING_PACKAGES, ';'))
  {
    PackageInfo packageInfo;
    if (!packageManager->TryGetPackageInfo(package, packageInfo))
    {
      neededPackages.push_back(package);
    }
  }
  if (!neededPackages.empty())
  {
    PathName bootstrappingDir = session->GetSpecialPath(SpecialPath::DistRoot) / MIKTEX_PATH_MIKTEX_BOOTSTRAPPING_DIR;
    if (Directory::Exists(bootstrappingDir))
    {
      PushTraceMessage("running MIKTEX_HOOK_BOOTSTRAPPING");
      EnsureInstaller();
      packageInstaller->SetRepository(bootstrappingDir.ToString());
      packageInstaller->UpdateDb();
      packageInstaller->SetFileList(neededPackages);
      packageInstaller->InstallRemove();
      packageInstaller = nullptr;
    }
  }
#endif
}

string kpsewhich_expand_path(const string& varname)
{
  string cmd = "kpsewhich --expand-path=";
#if defined(MIKTEX_UNIX)
  cmd += "\\";
#endif
  ProcessOutput<1024> output;
  int exitCode;
  if (!Process::ExecuteSystemCommand(cmd + "$" + varname, &exitCode, &output, nullptr) || exitCode != 0)
  {
    return string();
  }
  string result = output.StdoutToString();
  if (!result.empty() && result[result.length() - 1] == '\n')
  {
    result.erase(result.length() - 1);
  }
  return result;
}

string Concat(const initializer_list<string>& searchPaths, char separator = PathName::PathNameDelimiter)
{
  string result;
  for (const string& s : searchPaths)
  {
    if (s.empty())
    {
      continue;
    }
    if (!result.empty())
    {
      result += separator;
    }
    result += s;
  }
  return result;
}

vector<IniTeXMFApp::OtherTeX> IniTeXMFApp::FindOtherTeX()
{
  vector<OtherTeX> result;
  ProcessOutput<1024> version;
  int exitCode;
  if (Process::ExecuteSystemCommand("kpsewhich --version", &exitCode, &version, nullptr) && version.StdoutToString().find("MiKTeX") == string::npos)
  {
    OtherTeX otherTeX;
    otherTeX.name = "kpathsea";
    string versionString = version.StdoutToString();
    otherTeX.version = versionString.substr(0, versionString.find_first_of("\r\n"));
    StartupConfig otherConfig;
    otherConfig.userRoots = Concat({
      kpsewhich_expand_path("TEXMFHOME")
    });
    otherConfig.commonRoots = Concat({
      kpsewhich_expand_path("TEXMFLOCAL"),
      kpsewhich_expand_path("TEXMFDEBIAN"),
      kpsewhich_expand_path("TEXMFDIST")
    });
    otherTeX.startupConfig = otherConfig;
    result.push_back(otherTeX);
  }
  return result;
}

void IniTeXMFApp::RegisterOtherRoots()
{
  vector<OtherTeX> otherTeXDists = FindOtherTeX();
  vector<PathName> otherRoots;
  for (const OtherTeX& other : otherTeXDists)
  {
    const string& roots = (session->IsAdminMode() ? other.startupConfig.commonRoots : other.startupConfig.userRoots);
    for (const string& r : StringUtil::Split(roots, PathName::PathNameDelimiter))
    {
      otherRoots.push_back(r);
    }
  }
  if (otherRoots.empty())
  {
  }
  else
  {
    RegisterRoots(otherRoots, true, true);
  }
}

void IniTeXMFApp::CreatePortableSetup(const PathName& portableRoot)
{
  unique_ptr<Cfg> config(Cfg::Create());
  config->PutValue("Auto", "Config", "Portable");
  PathName configDir(portableRoot);
  configDir /= MIKTEX_PATH_MIKTEX_CONFIG_DIR;
  Directory::Create(configDir);
  PathName startupFile(portableRoot);
  startupFile /= MIKTEX_PATH_STARTUP_CONFIG_FILE;
  config->Write(startupFile, T_("MiKTeX startup configuration"));
  PathName tempDir(portableRoot);
  tempDir /= MIKTEX_PATH_MIKTEX_TEMP_DIR;
  if (!Directory::Exists(tempDir))
  {
    Directory::Create(tempDir);
  }
}

void IniTeXMFApp::WriteReport()
{
  if (xml)
  {
    xmlWriter.StartDocument();
    xmlWriter.StartElement("miktexreport");
  }
  ReportMiKTeXVersion();
  ReportOSVersion();
  ReportRoots();
  if (xml)
  {
    ReportFndbFiles();
  }
#if defined(MIKTEX_WINDOWS)
  if (xml)
  {
    ReportEnvironmentVariables();
  }
#endif
  ReportBrokenPackages();
  if (xml)
  {
    xmlWriter.EndElement();
  }
}

bool IniTeXMFApp::OnFndbItem(const PathName& parent, const string& name, const string& info, bool isDirectory)
{
  if (recursive)
  {
    PathName path(parent, name);
    const char* lpszRel = Utils::GetRelativizedPath(path.GetData(), enumDir.GetData());
    if (!isDirectory)
    {
      if (info.empty())
      {
        cout << lpszRel << endl;
      }
      else
      {
        if (csv)
        {
          cout << lpszRel << ";" << info << endl;
        }
        else
        {
          cout << lpszRel << " (\"" << info << "\")" << endl;
        }
      }
    }
    if (isDirectory)
    {
      Fndb::Enumerate(path, this);
    }
  }
  else
  {
    if (info.empty())
    {
      cout << (isDirectory ? "D" : " ") << " " << name << endl;
    }
    else
    {
      cout << (isDirectory ? "D" : " ") << " " << name << " (\"" << info << "\")" << endl;
    }
  }
  return true;
}

void IniTeXMFApp::Run(int argc, const char* argv[])
{
  vector<string> addFiles;
  vector<string> showConfigValue;
  vector<string> setConfigValues;
  vector<string> createConfigFiles;
  vector<string> editConfigFiles;
  vector<string> formats;
  vector<string> formatsByName;
  vector<string> listDirectories;
  vector<string> removeFiles;
  vector<string> updateRoots;
  vector<PathName> registerRoots;
  vector<PathName> unregisterRoots;
  string defaultPaperSize;
  string engine;
  string logFile;
  string portableRoot;

  bool optClean = false;
  bool optDump = false;
  bool optDumpByName = false;
  bool optFindOtherTeX = false;
  bool optForce = false;
  bool optMakeLanguageDat = false;
  bool optMakeMaps = false;
  bool optListFormats = false;
  bool optListModes = false;
  bool optMakeLinks = isTexlinksMode;
  LinkCategoryOptions linkCategories;
#if defined(MIKTEX_WINDOWS)
  bool optNoRegistry = false;
#endif
  bool optPortable = false;
  bool optRegisterOtherRoots = false;
  bool optRegisterShellFileTypes = false;
  bool optRemoveLinks = false;
  bool optModifyPath = false;
  bool optReport = false;
  bool optUnRegisterShellFileTypes = false;
  bool optUpdateFilenameDatabase = isMktexlsrMode;
  bool optVersion = false;

  const struct poptOption* aoptions;

  if (setupWizardRunning)
  {
    aoptions = aoption_setup;
  }
  else if (updateWizardRunning)
  {
    aoptions = aoption_update;
  }
  else if (isMktexlsrMode)
  {
    aoptions = aoption_mktexlsr;
  }
  else if (isTexlinksMode)
  {
    aoptions = aoption_texlinks;
  }
  else
  {
    aoptions = aoption_user;
  }

  PoptWrapper popt(argc, argv, aoptions);

  int option;

  while ((option = popt.GetNextOpt()) >= 0)
  {
    string optArg = popt.GetOptArg();
    switch (option)
    {

    case OPT_ADD_FILE:

      addFiles.push_back(optArg);
      break;

    case OPT_CLEAN:
      optClean = true;
      break;

    case OPT_CREATE_CONFIG_FILE:

      createConfigFiles.push_back(optArg);
      break;

    case OPT_CSV:
      csv = true;
      break;

    case OPT_DEFAULT_PAPER_SIZE:

      defaultPaperSize = optArg;
      break;

    case OPT_DISABLE_INSTALLER:
      enableInstaller = TriState::False;
      break;

    case OPT_DUMP:

      if (!optArg.empty())
      {
        formats.push_back(optArg);
      }
      optDump = true;
      break;

    case OPT_DUMP_BY_NAME:

      formatsByName.push_back(optArg);
      optDumpByName = true;
      break;

    case OPT_EDIT_CONFIG_FILE:

      editConfigFiles.push_back(optArg);
      break;

    case OPT_ENABLE_INSTALLER:
      enableInstaller = TriState::True;
      break;

    case OPT_ENGINE:
      engine = optArg;
      break;

    case OPT_FIND_OTHER_TEX:
      optFindOtherTeX = true;
      break;

    case OPT_FORCE:

      optForce = true;
      break;

    case OPT_COMMON_INSTALL:

      startupConfig.commonInstallRoot = optArg;
      break;

    case OPT_USER_INSTALL:

      startupConfig.userInstallRoot = optArg;
      break;

    case OPT_LIST_DIRECTORY:

      listDirectories.push_back(optArg);
      break;

    case OPT_LIST_FORMATS:

      optListFormats = true;
      break;

    case OPT_LIST_MODES:

      optListModes = true;
      break;

    case OPT_COMMON_DATA:

      startupConfig.commonDataRoot = optArg;
      break;

    case OPT_COMMON_CONFIG:

      startupConfig.commonConfigRoot = optArg;
      break;

    case OPT_USER_DATA:

      startupConfig.userDataRoot = optArg;
      break;

    case OPT_USER_CONFIG:

      startupConfig.userConfigRoot = optArg;
      break;

    case OPT_LOG_FILE:

      logFile = optArg;
      break;

    case OPT_MKLANGS:

      optMakeLanguageDat = true;
      break;

    case OPT_MKLINKS:

      optMakeLinks = true;
      if (optArg.empty())
      {
        linkCategories.Set();
      }
      else if (optArg == "formats")
      {
        linkCategories += LinkCategory::Formats;
      }
      else if (optArg == "miktex")
      {
        linkCategories += LinkCategory::MiKTeX;
      }
      else if (optArg == "scripts")
      {
        linkCategories += LinkCategory::Scripts;
      }
      else
      {
        MIKTEX_FATAL_ERROR(T_("Unknown link category."));
      }      
      break;

    case OPT_MKMAPS:

      optMakeMaps = true;
      break;

    case OPT_MODIFY_PATH:

      optModifyPath = true;
      break;

#if defined(MIKTEX_WINDOWS)
    case OPT_NO_REGISTRY:

      optNoRegistry = true;
      break;
#endif

    case OPT_PORTABLE:

      portableRoot = optArg;
      optPortable = true;
      break;

    case OPT_PRINT_ONLY:

      printOnly = true;
      break;

    case OPT_QUIET:

      quiet = true;
      break;

    case OPT_RECURSIVE:

      recursive = true;
      break;

    case OPT_REGISTER_OTHER_ROOTS:

      optRegisterOtherRoots = true;
      break;

    case OPT_REGISTER_SHELL_FILE_TYPES:

      optRegisterShellFileTypes = true;
      break;

    case OPT_REGISTER_ROOT:

      registerRoots.push_back(optArg);
      break;

    case OPT_REMOVE_FILE:

      removeFiles.push_back(optArg);
      break;

    case OPT_REMOVE_LINKS:

      optRemoveLinks = true;
      linkCategories.Set();
      break;
      
    case OPT_REPORT:

      optReport = true;
      break;

    case OPT_RMFNDB:

      removeFndb = true;
      break;

    case OPT_SET_CONFIG_VALUE:

      setConfigValues.push_back(optArg);
      break;

    case OPT_SHOW_CONFIG_VALUE:

      showConfigValue.push_back(optArg);
      break;

    case OPT_COMMON_ROOTS:

      startupConfig.commonRoots = optArg;
      break;

    case OPT_USER_ROOTS:

      startupConfig.userRoots = optArg;
      break;

    case OPT_ADMIN:

      if (!session->IsAdminMode())
      {
        MIKTEX_UNEXPECTED();
      }
      break;

    case OPT_UNREGISTER_ROOT:

      unregisterRoots.push_back(optArg);
      break;

    case OPT_UNREGISTER_SHELL_FILE_TYPES:

      optUnRegisterShellFileTypes = true;
      break;

    case OPT_UPDATE_FNDB:

      optUpdateFilenameDatabase = true;
      if (!optArg.empty())
      {
        updateRoots.push_back(optArg);
      }
      break;

    case OPT_VERBOSE:

      verbose = true;
      break;

    case OPT_VERSION:

      optVersion = true;
      break;

    case OPT_XML:
      xml = true;
      break;

    }
  }

  if (option != -1)
  {
    string msg = popt.BadOption(POPT_BADOPTION_NOALIAS);
    msg += ": ";
    msg += popt.Strerror(option);
    FatalError("%s", msg.c_str());
  }

  if (!popt.GetLeftovers().empty())
  {
    FatalError(T_("This utility does not accept non-option arguments."));
  }

  if (optVersion)
  {
    cout
      << Utils::MakeProgramVersionString(TheNameOfTheGame, MIKTEX_COMPONENT_VERSION_STR) << endl
      << endl
      << "Copyright (C) 1996-2018 Christian Schenk" << endl
      << "This is free software; see the source for copying conditions.  There is NO" << endl
      << "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE." << endl;
    return;
  }

  if (!logFile.empty())
  {
    if (File::Exists(logFile))
    {
      logStream.Attach(File::Open(logFile, FileMode::Append, FileAccess::Write));
    }
    else
    {
      logStream.Attach(File::Open(logFile, FileMode::Create, FileAccess::Write));
    }
  }

  if (optPortable)
  {
    CreatePortableSetup(portableRoot);
  }

  if (!startupConfig.userRoots.empty()
    || !startupConfig.userDataRoot.Empty()
    || !startupConfig.userConfigRoot.Empty()
    || !startupConfig.userInstallRoot.Empty()
    || !startupConfig.commonRoots.empty()
    || !startupConfig.commonDataRoot.Empty()
    || !startupConfig.commonConfigRoot.Empty()
    || !startupConfig.commonInstallRoot.Empty())
  {
#if defined(MIKTEX_WINDOWS)
    SetTeXMFRootDirectories(optNoRegistry);
#else
    SetTeXMFRootDirectories();
#endif
  }

  if (!defaultPaperSize.empty())
  {
    session->SetDefaultPaperSize(defaultPaperSize);
  }

  if (optDump)
  {
    MakeFormatFiles(formats);
  }

  if (optDumpByName)
  {
    MakeFormatFilesByName(formatsByName, engine);
  }

#if defined(MIKTEX_WINDOWS)
  if (optRegisterShellFileTypes)
  {
    RegisterShellFileTypes(true);
  }
#endif

#if defined(MIKTEX_WINDOWS)
  if (optUnRegisterShellFileTypes)
  {
    RegisterShellFileTypes(false);
  }
#endif

  if (optModifyPath)
  {
    ModifyPath();
  }

  if (optMakeLanguageDat)
  {
    MakeLanguageDat(optForce);
  }

  if (optMakeLinks || optRemoveLinks)
  {
    ManageLinks(linkCategories, optRemoveLinks, optForce);
#if defined(MIKTEX_UNIX)
    if (optMakeLinks)
    {
      MakeScriptsExecutable();
    }
#endif
  }

  if (optMakeMaps)
  {
    MakeMaps(optForce);
  }

  for (const string& fileName : addFiles)
  {
    Verbose(T_("Adding %s to the file name database..."), Q_(fileName));
    PrintOnly("fndbadd %s", Q_(fileName));
    if (!printOnly)
    {
      if (!Fndb::FileExists(fileName))
      {
        Fndb::Add(fileName);
      }
      else
      {
        Warning(T_("%s is already recorded in the file name database"), Q_(fileName));
      }
    }
  }

  for (const string& fileName : removeFiles)
  {
    Verbose(T_("Removing %s from the file name database..."), Q_(fileName));
    PrintOnly("fndbremove %s", Q_(fileName));
    if (!printOnly)
    {
      if (Fndb::FileExists(fileName))
      {
        Fndb::Remove(fileName);
      }
      else
      {
        Warning(T_("%s is not recorded in the file name database"), Q_(fileName));
      }
    }
  }

  if (removeFndb)
  {
    RemoveFndb();
  }

  if (!unregisterRoots.empty())
  {
    RegisterRoots(unregisterRoots, false, false);
  }

  if (!registerRoots.empty())
  {
    RegisterRoots(registerRoots, false, true);
  }

  if (optUpdateFilenameDatabase)
  {
    if (updateRoots.empty())
    {
      unsigned nRoots = session->GetNumberOfTEXMFRoots();
      for (unsigned r = 0; r < nRoots; ++r)
      {
        if (session->IsAdminMode())
        {
          if (session->IsCommonRootDirectory(r))
          {
            UpdateFilenameDatabase(r);
          }
          else
          {
            Verbose(T_("Skipping user root directory (%s)..."), Q_(session->GetRootDirectoryPath(r)));
          }
        }
        else
        {
          if (!session->IsCommonRootDirectory(r) || session->IsMiKTeXPortable())
          {
            UpdateFilenameDatabase(r);
          }
          else
          {
            Verbose(T_("Skipping common root directory (%s)..."), Q_(session->GetRootDirectoryPath(r)));
          }
        }
      }
      PackageInfo test;
      bool havePackageDatabase = packageManager->TryGetPackageInfo("miktex-tex", test);
      if (!havePackageDatabase)
      {
        if (enableInstaller == TriState::True)
        {
          EnsureInstaller();
          packageInstaller->UpdateDb();
        }
        else
        {
          Warning(T_("The local package database does not exist."));
        }
      }
      else
      {
        packageManager->CreateMpmFndb();
      }
    }
    else
    {
      for (const string& r : updateRoots)
      {
        UpdateFilenameDatabase(r);
      }
    }
  }

  for (const string& dir : listDirectories)
  {
    enumDir = dir;
    Fndb::Enumerate(dir, this);
  }

  for (const string& fileName : createConfigFiles)
  {
    CreateConfigFile(fileName, false);
  }

  for (const string& v : setConfigValues)
  {
    SetConfigValue(v);
  }

  for (const string& v : showConfigValue)
  {
    ShowConfigValue(v);
  }

  for (const string& fileName : editConfigFiles)
  {
    CreateConfigFile(fileName, true);
  }

  if (optListFormats)
  {
    ListFormats();
  }

  if (optListModes)
  {
    ListMetafontModes();
  }

  if (optReport)
  {
    WriteReport();
  }

  if (optFindOtherTeX)
  {
    vector<OtherTeX> otherTeXs = FindOtherTeX();
    for (const OtherTeX& otherTeX : otherTeXs)
    {
      cout << "Found OtherTeX: " << otherTeX.name << "\n";
      cout << "  Version: " << otherTeX.version << "\n";
      cout << "  UserRoots: " << otherTeX.startupConfig.userRoots << "\n";
      cout << "  CommonRoots: " << otherTeX.startupConfig.commonRoots << "\n";
    }
  }

  if (optRegisterOtherRoots)
  {
    RegisterOtherRoots();
  }

  if (optClean)
  {
    Clean();
  }
}

#if defined(_UNICODE)
#  define MAIN wmain
#  define MAINCHAR wchar_t
#else
#  define MAIN main
#  define MAINCHAR char
#endif

int MAIN(int argc, MAINCHAR* argv[])
{
  try
  {
    vector<string> utf8args;
    utf8args.reserve(argc);
    vector<const char*> newargv;
    newargv.reserve(argc + 1);
    for (int idx = 0; idx < argc; ++idx)
    {
#if defined(_UNICODE)
      utf8args.push_back(StringUtil::WideCharToUTF8(argv[idx]));
#elif defined(MIKTEX_WINDOWS)
      utf8args.push_back(StringUtil::AnsiToUTF8(argv[idx]));
#else
      utf8args.push_back(argv[idx]);
#endif
      newargv.push_back(utf8args[idx].c_str());
    }
    newargv.push_back(nullptr);
    IniTeXMFApp app;
    app.Init(argc, &newargv[0]);
    LOG4CXX_INFO(logger, "starting with command line: " << CommandLineBuilder(utf8args));
    app.Run(argc, &newargv[0]);
    app.Finalize(false);
    LOG4CXX_INFO(logger, "finishing with exit code 0");
    logger = nullptr;
    return 0;
  }
  catch (const MiKTeXException& e)
  {
    if (logger != nullptr && isLog4cxxConfigured)
    {
      LOG4CXX_FATAL(logger, e.what());
      LOG4CXX_FATAL(logger, "Info: " << e.GetInfo());
      LOG4CXX_FATAL(logger, "Source: " << e.GetSourceFile());
      LOG4CXX_FATAL(logger, "Line: " << e.GetSourceLine());
    }
    else
    {
      cerr << e.what() << endl
           << "Info: " << e.GetInfo() << endl
           << "Source: " << e.GetSourceFile() << endl
           << "Line: " << e.GetSourceLine() << endl;
    }
    Sorry();
    logger = nullptr;
    return 1;
  }
  catch (const exception& e)
  {
    if (logger != nullptr && isLog4cxxConfigured)
    {
      LOG4CXX_FATAL(logger, e.what());
    }
    else
    {
      cerr <<  e.what() << endl;
    }
    Sorry();
    logger = nullptr;
    return 1;
  }
  catch (int exitCode)
  {
    logger = nullptr;
    return exitCode;
  }
}
