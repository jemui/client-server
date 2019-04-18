// $Id: cix.cpp,v 1.7 2019-02-07 15:14:37-08 - - $
// Jeanette Mui, jemui@ucsc.edu

#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
using namespace std;

#include <libgen.h>
#include <sys/types.h>
#include <unistd.h>

#include "protocol.h"
#include "logstream.h"
#include "sockets.h"

logstream log (cout);
struct cix_exit: public exception {};

unordered_map<string,cix_command> command_map {
   {"exit", cix_command::EXIT},
   {"get" , cix_command::GET },
   {"help", cix_command::HELP},
   {"ls"  , cix_command::LS  },
   {"put" , cix_command::PUT },
   {"rm"  , cix_command::RM  },
   {"ack" , cix_command::ACK },
};

static const string help = R"||(
exit         - Exit the program.  Equivalent to EOF.
get filename - Copy remote file to local host.
help         - Print help summary.
ls           - List names of files on remote server.
put filename - Copy local file to remote host.
rm filename  - Remove file from remote server.
)||";

void cix_help() {
   cout << help;
}

void cix_get (client_socket& server, string filename) {
   cix_header header;
   header.command = cix_command::GET;
   
   strncpy(header.filename, filename.c_str(), 
         sizeof(header.filename));

   log << "sending header " << header << endl;
   send_packet (server, &header, sizeof header);
   recv_packet (server, &header, sizeof header);
   log << "received header " << header << endl;

   if (header.command != cix_command::FILEOUT) {
      log << "get -l: " 
            << strerror (errno) << endl;
      header.command = cix_command::NAK;
      header.nbytes = errno;
      send_packet (server, &header, sizeof header);
   } else {
      auto buffer = make_unique<char[]> (header.nbytes + 1);
      recv_packet (server, buffer.get(), header.nbytes);
      log << "received " << header.nbytes << " nbytes" << endl;
      buffer[header.nbytes] = '\0';

      ofstream outfile(filename);
      outfile << buffer.get() << endl;
      outfile.close();
   }
}

void cix_ls (client_socket& server) {
   cix_header header;
   header.command = cix_command::LS;

   log << "sending header " << header << endl;
   send_packet (server, &header, sizeof header);
   recv_packet (server, &header, sizeof header);
   log << "received header " << header << endl;

   if (header.command != cix_command::LSOUT) {
      log << "sent LS, server did not return LSOUT" << endl;
      log << "server returned " << header << endl;
   }else {
      auto buffer = make_unique<char[]> (header.nbytes + 1);
      recv_packet (server, buffer.get(), header.nbytes);
      log << "received " << header.nbytes << " bytes" << endl;
      buffer[header.nbytes] = '\0';
      cout << buffer.get();
   }
}

void cix_put (client_socket& server, string filename) {
   cix_header header;
   string put_output;
   char buffer[0x1000];

   // copy filename to header.filename
   strncpy(header.filename, filename.c_str(), 
         sizeof(header.filename));

   ifstream ifs(filename, ifstream::binary);
   if(!ifs) {
      log << "put -l: " 
            << strerror (errno) << endl;
      header.command = cix_command::NAK;
      header.nbytes = errno;
      send_packet (server, &header, sizeof header);
      return;
   } else {
      // open file
      ifs.open(filename);

      // get content of file
      string content( (std::istreambuf_iterator<char>(ifs) ),
                        (std::istreambuf_iterator<char>()  ) );
      ifs.close();

      // load into buffer 
      strncpy(buffer, content.c_str(), sizeof (buffer));
      put_output.append(buffer);
   }

   header.command = cix_command::PUT;
   header.nbytes = put_output.size();

   log << "sending header " << header << endl;
   send_packet (server, &header, sizeof header);
   send_packet (server, put_output.c_str(), put_output.size());

   memset (header.filename, 0, FILENAME_SIZE);
   
   recv_packet (server, &header, sizeof header);
   if(header.command == cix_command::ACK)
      log << "received header " << header << endl;
   else {
      // nak received
      log << "put -l: " << strerror (errno) << endl;
      header.command = cix_command::NAK;
      header.nbytes = errno;
      send_packet (server, &header, sizeof header);
   }
}

void cix_rm (client_socket& server, string filename) {
   cix_header header;
   header.command = cix_command::RM; 

   // copy filename to header.filename
   strncpy(header.filename, filename.c_str(), 
         sizeof(header.filename));

   log << "sending header " << header << endl;
   send_packet (server, &header, sizeof header);
   recv_packet (server, &header, sizeof header);
   log << "received header " << header << endl;

   if(header.command == cix_command::NAK) {
      log << "rm -l: " 
            << strerror (errno) << endl;
   }
}

void usage() {
   cerr << "Usage: " << log.execname() << " [host] [port]" << endl;
   throw cix_exit();
}

int main (int argc, char** argv) {
   log.execname (basename (argv[0]));
   log << "starting" << endl;
   vector<string> args (&argv[1], &argv[argc]);
   if (args.size() > 2) usage();
   string host = get_cix_server_host (args, 0);
   in_port_t port = get_cix_server_port (args, 1);
   log << to_string (hostinfo()) << endl;
   try {
      log << "connecting to " << host << " port " << port << endl;
      client_socket server (host, port);
      log << "connected to " << to_string (server) << endl;
      for (;;) {
         string line;
         string filename;
         getline (cin, line);
         if (cin.eof()) throw cix_exit();
         filename = line.substr(line.find_first_of(' ')+1);

         if(line[0] == 'r' and line[1] == 'm')
            line = line.substr(0,2);
         else if(line[0] == 'e')
            line = line.substr(0,4);
         else if(line[0] != 's')
            line = line.substr(0,3);

         log << "command " << line << endl;
         const auto& itor = command_map.find (line);
         cix_command cmd = itor == command_map.end()
                         ? cix_command::ERROR : itor->second;
         switch (cmd) {
            case cix_command::EXIT:
               throw cix_exit();
               break;
            case cix_command::HELP:
               cix_help();
               break;
            case cix_command::GET:
               cix_get (server, filename);
               break;
            case cix_command::LS:
               cix_ls (server);
               break;
            case cix_command::PUT: 
               cix_put (server, filename);
               break;
            case cix_command::RM:
               cix_rm (server, filename);
               break;
            default:
               log << line << ": invalid command" << endl;
               break;
         }
      }
   }catch (socket_error& error) {
      log << error.what() << endl;
   }catch (cix_exit& error) {
      log << "caught cix_exit" << endl;
   }
   log << "finishing" << endl;
   return 0;
}

