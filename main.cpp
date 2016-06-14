#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>

// C++ stuff
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>

// inotify support
#include <sys/inotify.h>

//using namespace std;

#define DAEMON_NAME "pacman-mirror-daemon"

//const char MonitorDir[]   = "/tmp/pacman.d/mirrorlist.pacnew";
const char DaemonDir[]     = "/var/tmp/pacman-mirror-daemon";
const char DaemonLock[]    = "pacman-mirror-daemon.lock";
const char MonitorDir[]   = "/etc/pacman.d";
const char MonitorFile[]   = "mirrorlist.pacnew";
const char TargetFile[]    = "mirrorlist";
constexpr int eventSize = sizeof(struct inotify_event);
constexpr int bufLen = (1024 * (eventSize + 16));
const char UKHeader[] = "## United Kingdom";

int lockFileH = -1;

void process()
{
    syslog (LOG_NOTICE, "Writing to Art's' Syslog");
}

bool monitor()
{
    syslog(LOG_NOTICE, "Entering monitor");

    int _inotifyFD = inotify_init();
    if (_inotifyFD < 0) {
        syslog(LOG_NOTICE, "Failed to obtain inotify instance, leaving");
        return false;
    }

    char buffer[bufLen];
    // Interested only in new files
    int _watchFD = inotify_add_watch(_inotifyFD, MonitorDir, IN_MODIFY | IN_CREATE | IN_DELETE);
    int _watchTerm = inotify_add_watch(_inotifyFD, "/var/tmp/pacman-mirror-daemon", IN_CLOSE_WRITE|IN_CLOSE_NOWRITE|IN_CLOSE|IN_MODIFY);

    bool _keepRunning = true;
    while (_keepRunning) {
        int i=0;
        int length = read(_inotifyFD, buffer, bufLen);

        syslog(LOG_NOTICE, "Got notification; length=%d", length);

        while (i < length) {
            struct inotify_event *_event = (inotify_event*) &buffer[i];

            syslog(LOG_NOTICE, "Notification for: '%s'", _event->name);

            if (_event->len) {
                syslog(LOG_NOTICE, "Processing notification entry (len=%d)", _event->len);

                if (_event->mask & IN_CREATE) {
                    syslog(LOG_NOTICE, "New file '%s' was created", _event->name);
                }
                else if (_event->mask & IN_MODIFY) {
                    syslog(LOG_NOTICE, "Existing file '%s' was modified", _event->name);
                }
                else if (_event->mask & IN_DELETE) {
                    syslog(LOG_NOTICE, "Existing file '%s' was deleted", _event->name);
                }
            }
            else {
                syslog(LOG_NOTICE, "Empty event received, something wrong?");
            }

            // Real thing starts here
            std::string _fileName(_event->name);
            if (_fileName.find(DaemonLock) != std::string::npos) {
                // Leaving signal changes
                syslog(LOG_NOTICE, "Lock file changed - must be leave signal");
                _keepRunning = false;
                break;
            }
            else if (_fileName.find(MonitorFile) != std::string::npos && (_event->mask & IN_MODIFY)) {
                syslog(LOG_NOTICE, "Got monitor file, processing");
                // Processing actual file here
                std::string _inFullPath(MonitorDir);
                std::string _outFullPath(MonitorDir);

                _inFullPath.append("/").append(_fileName);
                _outFullPath.append("/").append(TargetFile);

                std::ifstream _infile(_inFullPath);
                if (!_infile) {
                    syslog(LOG_NOTICE, "Failed to open input file for reading");
                    break;
                }
                std::string _fileContent{ std::istreambuf_iterator<char>(_infile), std::istreambuf_iterator<char>() };

                // Find UK
                std::size_t _ukPos = _fileContent.find(UKHeader);
                if (_ukPos == std::string::npos) {
                    syslog(LOG_NOTICE, "Did not found '## United Kingdom', continue monitoring");
                    break;
                }
                std::size_t _currPos = _fileContent.find_first_of("\n", _ukPos);
                std::size_t _nextCountry = _fileContent.find("\n\n## ", _currPos);
                syslog(LOG_NOTICE, "Starting loop with currPos=%d and nextCountry=%d", _currPos, _nextCountry);
                while(_currPos < _nextCountry) {
                    _currPos = _fileContent.find_first_of('#', _currPos);
                    if (_currPos == std::string::npos) {
                        break;
                    }
                    if (_currPos == _nextCountry) {
                        break;
                    }
                    syslog(LOG_NOTICE, "Erasing '#' from string ");
                    _fileContent.erase(_currPos, 1);
                    --_nextCountry;
                }
                std::ofstream _outfile(_outFullPath);
                if (_outfile) {
                    _outfile << _fileContent;
                    _outfile.flush();
                    _outfile.close();
                }
                else {
                    syslog(LOG_NOTICE, "Failed to open output file for writting");
                }
            }

            i += (eventSize + _event->len);
        }
    }
    syslog(LOG_NOTICE, "Cleaning up and leaving");
    inotify_rm_watch(_inotifyFD, _watchFD);
    inotify_rm_watch(_inotifyFD, _watchTerm);
    close(_inotifyFD);
    return false;
}

void signalHandler(int signal)
{
    switch(signal) {
    case SIGHUP:
        syslog(LOG_NOTICE, "Hangup signal received");
        break;
    case SIGTERM:
        syslog(LOG_NOTICE, "Terminate signal received, leaving");
        close(lockFileH);
        // Give time to monitor to cleanly finish
        sleep(2);
        exit(EXIT_SUCCESS);
        break;
    default:
        syslog(LOG_NOTICE, "Unhandled signal received: %s", strsignal(signal));
        break;
    }
}

int main(int argc, char *argv[]) {

    //Set our Logging Mask and open the Log
    setlogmask(LOG_UPTO(LOG_NOTICE));
    openlog(DAEMON_NAME, LOG_CONS | LOG_NDELAY | LOG_PERROR | LOG_PID, LOG_USER);

    syslog(LOG_NOTICE, "Entering Daemon");

    pid_t pid, sid;

   //Fork the Parent Process
    pid = fork();

    if (pid < 0) { exit(EXIT_FAILURE); }

    //We got a good pid, Close the Parent Process
    if (pid > 0) { exit(EXIT_SUCCESS); }

    //Change File Mask
	

    //Create a new Signature Id for our child
    sid = setsid();
    if (sid < 0) { exit(EXIT_FAILURE); }

    //Change Directory
    //If we cant find the directory we exit with failure.
    if ((chdir(DaemonDir)) < 0) { exit(EXIT_FAILURE); }

    //Close Standard File Descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // For running only single copy - create lock file
    int lockFileH = open(DaemonLock, O_RDWR|O_CREAT, 0640);
    if (lockFileH < 0)
        // Cannot open lock file - exiting
        exit(EXIT_FAILURE);

    if (lockf(lockFileH, F_TLOCK, 0) < 0)
        // Cannot lock - exiting
        exit(EXIT_FAILURE);

    char _sPid[40];
    sprintf(_sPid, "%d\n", getpid());
    write(lockFileH, _sPid, strlen(_sPid));

    sigset_t ignoreSet;
    sigemptyset(&ignoreSet);
    sigaddset(&ignoreSet, SIGCHLD); // ignore child signals
    sigaddset(&ignoreSet, SIGTSTP); // ignore tty stop
    sigaddset(&ignoreSet, SIGTTOU); // ignore tty background writes
    sigaddset(&ignoreSet, SIGTTIN); // ignore tty bacground reads
    sigprocmask(SIG_BLOCK, &ignoreSet, NULL); // Block above signals

    // Setup signal handler
    signal(SIGHUP, signalHandler);	// hangup signal
    signal(SIGTERM, signalHandler);	// term signal
    signal(SIGINT, signalHandler);	// interrupt signal


    //----------------
    //Main Process
    //----------------
    while(monitor()){
        syslog(LOG_NOTICE, "Processed event successfully, continue monitoring");
    }
    syslog(LOG_NOTICE, "Finished with monitor, exiting daemon");

    //Close the log
    closelog ();
}
