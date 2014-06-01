#include <cstdlib>
#include <iostream>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h> // gethostbyname
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace std;


int controlSocket; //ridici spojeni
int dataSocket; //datove spojeni

FILE *ctrlSocketStreamIn;
FILE *ctrlSocketStreamOut;

enum E_ERR {
    E_OK, E_ARG, E_DNS, E_SOCKET, E_CONNECT, E_SOCK_STREAM, E_NEGATIVE_RESPONSE,
    E_LOGIN_INCORRECT, E_EPSV, E_CWD, E_LOGOUT
}; 

bool startswith(string haystack, string needle) {
    string sub = haystack.substr(0, needle.length());
    
    if (sub == needle)
        return true;
    
    return false;
}


class Credentials {
public:
    string username, password, host, path, controlIp, dataIp, port;
    int intPort;
    
    Credentials(string username, string password, string host, string path, string port) {
        this->username = username;
        this->password = password;
        this->port = port;
        this->host = host;
        this->path = path;
        this->controlIp = "";
        this->dataIp = "";
        
        this->intPort = atoi(port.c_str());
    }
    
    void setControlIp(string ip) {
        this->controlIp = ip;
    }
    
    void setDataIp(string ip) {
        this->dataIp = ip;
    }
    
    bool isValid() {
        return false;
    }
    
    void print() {
       cout << "username:"  << this->username << endl;
       cout << "password:"  << this->password << endl;
       cout << "host:    "  << this->host << endl;
       cout << "port:    "  << this->port << endl;
       cout << "path:    "  << this->path << endl;
    }
    
};

#define BUFFER_SIZE 1024
char buffer[BUFFER_SIZE];


Credentials parseArg(int argc, char** argv) {
    
    if (argc != 2)
        throw E_ARG;
    
    string url = argv[1];
    string user, pass, host, port, path = "";
    bool hasProtocol = false;
    
    if ( startswith(url, "ftp://")) {
        url = url.substr(6);
        hasProtocol = true;
    }
    
    int len = url.length();
    
    /* Hledam PATH */
    int start = url.find('/');
    int start2 = 0;
    
    if (start < 0 || start > len) {
        path = "";
    } else {
        path = url.substr(start);
        url = url.substr(0, start);
    }
    int startAt = url.find('@');
    string login = "";
    
    if ( startAt >= 0) {
        login = url.substr(0, startAt);
        url = url.substr(startAt+1);
        
        if ( ! login.empty() ) {
            int delim = login.find(':');
            if (delim < 0) {
                throw(E_ARG);
            }
            
            user = login.substr(0, delim);
            pass = login.substr(delim+1);
        }
    }
    
    if ( !hasProtocol && startAt >= 0) {
        throw(E_ARG);
    }
        
    // predpoklad: zde uz mam pouze adresu a port
    startAt = url.rfind(':');
    if (startAt > 0){
        port = url.substr(startAt+1);
        url = url.substr(0, startAt);
    }
    
    if (user == "" ) {
        user = "anonymous";
        pass = "no-pass";
    }
    
    if (port == "") {
        port = "21";
    }
    
    host = url;
    
    
    Credentials cr = Credentials(user, pass, host, path, port );
    return cr;
}

int m_write( FILE* stream, string cmd) {
    fputs(cmd.c_str(), stream);
    fflush(stream);
    
    return 0;
}

string m_read(FILE *streamIn) {
    
    char line[1024];
    char strcode[4];
    
    memset(strcode, 0, 4);
    
    string res = "";
    
    do {
        memset(line, 0, 1024);
        fgets(line, 1024, streamIn);
        
        res.append(line);
    } while ( !isdigit(line[0]) || line[3] == '-');
    
    
    return res;
}

string m_read_list (int socket) {
    
    char buffer[BUFFER_SIZE] = {0};
    string res = "";
    
    int bytes;
    
    while (true) {
        bytes = read(socket, buffer, BUFFER_SIZE-1);
        
        if (bytes == 0)
            break;
        else if( bytes < 0)
            throw(-1);
        
        buffer[bytes] = '\0';
        
        res.append(buffer);
    }

    return res;
}

bool getDataPort(string str) {
    //cout << str;
    return 0;
}

int parseReturnCode(string msg) {
    return atoi(msg.substr(0, 3).c_str());
}

int parsePortNum( string str ) {

  string::iterator it;
  int index = 0;
  
  string res = "";
  
  int len = str.length();
  
  for ( int i = 0; i < len; ++i) {

        if (isdigit( str[i])) {
            res += str[i];
            continue;
        }
        // sem se dostane pouze kdyz ch neni cislo
        if ( res.length() != 0 )
            break;
  }
  
  return atoi(res.c_str());
    
}


void assertRetcode(string str, int val) {
    
    int code = parseReturnCode(str);
    
    if (code >= 400 && code <= 600)
        throw val;
}

void clean() {
    close(controlSocket);
    close(dataSocket);
    fclose(ctrlSocketStreamIn);
    fclose(ctrlSocketStreamOut);
}

int main(int argc, char** argv) {
    
    try {
        Credentials cr = parseArg(argc, argv);

        struct hostent *he = gethostbyname(cr.host.c_str());
        if (he == NULL) {
            cerr << "Unable to translate host name via DNS" << endl;
            throw(E_DNS);
        }

        if ((controlSocket = socket(AF_INET, SOCK_STREAM, 0 )) < 0) {
            cerr << "Error during control socket creation" << endl;
            throw(E_SOCKET);
        }

        /* Navazani ridiciho spojeni */
        struct sockaddr_in server_address;
        memset(&server_address, 0, sizeof(server_address));
        server_address.sin_family = AF_INET;
        server_address.sin_port = htons ( cr.intPort );

        //server_address.sin_addr.s_addr = inet_addr(cr.controlIp.c_str());

        memcpy(&server_address.sin_addr, he->h_addr_list[0], he->h_length);
	
        if ( connect(controlSocket, (struct sockaddr *) &server_address, sizeof(server_address)) < 0) {
            cerr << "Control socket connection failed" << endl;
            throw(E_CONNECT);
        }

        /* --------------------------------------------------------------- *
         * KOMUNIKACE                                                      *
         * --------------------------------------------------------------- */

        ctrlSocketStreamIn = fdopen(controlSocket, "r");
        ctrlSocketStreamOut = fdopen(controlSocket, "w");

        if ( ctrlSocketStreamIn == NULL || ctrlSocketStreamOut == NULL) {
            cerr << "Opening socket as stream failed" << endl;
            throw(E_SOCK_STREAM);
        }


        string res = "";


        //cout << " --- empty" << endl;
        int code;

        // prazdna operace, jen aby mi rekl, ze je pripraveny
        m_write(ctrlSocketStreamOut, "NOOP\r\n");
        res = m_read(ctrlSocketStreamIn);
        assertRetcode(res, E_NEGATIVE_RESPONSE);
        
        // dalsim ctenim zjistim, jestli se musim prihlasit nebo ne
        res = m_read(ctrlSocketStreamIn);
        code = parseReturnCode(res);
        
        m_write(ctrlSocketStreamOut, "USER " +cr.username +"\r\n");
        res = m_read(ctrlSocketStreamIn);

        //cout << " --- PASS" << endl;
        m_write(ctrlSocketStreamOut, "PASS "+cr.password+"\r\n");
        res = m_read(ctrlSocketStreamIn);
        assertRetcode(res, E_LOGIN_INCORRECT);
            

        m_write(ctrlSocketStreamOut, "EPSV\r\n");
        res = m_read(ctrlSocketStreamIn);
        assertRetcode(res, E_EPSV);

        int dataPort = parsePortNum( res.substr(3));
        /* ### INICIALIZACE DATOVEHO SPOJENI ### */
        if ((dataSocket = socket(AF_INET, SOCK_STREAM, 0 )) < 0) {
            throw(E_SOCKET);
        }

        struct sockaddr_in server_data_address;
        memset(&server_data_address, 0, sizeof(server_data_address));
        server_data_address.sin_family = AF_INET;
        server_data_address.sin_port = htons ( dataPort );

        //server_address.sin_addr.s_addr = inet_addr(cr.controlIp.c_str());

        memcpy(&server_data_address.sin_addr, he->h_addr_list[0], he->h_length);
        if ( connect(dataSocket, (struct sockaddr *) &server_data_address, sizeof(server_data_address)) < 0) {
            throw(E_CONNECT);
        }

        /* zmena adresare */
        if ( !cr.path.empty() ) {
           m_write(ctrlSocketStreamOut, "CWD " + cr.path + "\r\n");
           res = m_read(ctrlSocketStreamIn);
           assertRetcode(res, E_CWD);
        }

        /* LIST */
        m_write(ctrlSocketStreamOut, "LIST\r\n");
        string list = res = m_read_list(dataSocket);

        string cmd = "QUIT\r\n";
        if ( write(dataSocket, cmd.c_str(), strlen(cmd.c_str())) < 0) {
            throw(E_LOGOUT);
        }
        int bytes;
        if ( (bytes = read(controlSocket, buffer, BUFFER_SIZE)) < 0) {
            throw(E_LOGOUT);
        }
        buffer[bytes] = '\0';
        res = buffer;

        cout << list;
        
    } catch (E_ERR ex) {
        
        string msg;
        
        switch( ex ) {
            case E_OK:  msg = "Everything OK"; break;
            case E_ARG: msg = "Invalid argument format"; return ex; break;
            case E_DNS: msg = "Could not translate hostname via DNS service"; break;
            case E_SOCKET: msg = "Creating socket failed"; break;
            case E_CONNECT: msg = "Socket connecting failed"; break;
            case E_SOCK_STREAM: msg = "Opening socket as stream failed"; break;
            case E_NEGATIVE_RESPONSE: msg =  "Negative server response"; break;
            case E_LOGIN_INCORRECT: msg = "Incorrect login"; break;
            case E_EPSV: msg = "Switching to passive mode failed"; break;
            case E_CWD: msg = "Could not change working directory"; break;
            case E_LOGOUT: msg = "Logging out failed"; break;
            default: msg = "Unknown error"; break;
        }
        
        cerr << msg << endl;
        
        clean();
        return ex;
    } 
    
    
    clean();
    return 0;
}

