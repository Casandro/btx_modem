# btx_modem
A simple v.23 modem including the data link layer. This is an application for Asterisk


Copy the source file under src into the apps subdirectory of Asterisk (tested with Asterisk 13) and compile Asterisk with make && make install. You will then have an application v23 you can use from the dialplan. The parameter is the IP-Address and the port to connect to. E.g. V23(127.0.0.1 1234);
