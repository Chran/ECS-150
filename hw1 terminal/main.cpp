#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <termios.h>
#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <stdio.h>
#include <string>
#include <cstring>
#include <vector>
#include <stdlib.h>
#include <iostream>
#include <sstream>

using namespace std;

void ResetCanonicalMode(int fd, struct termios *savedattributes){
    tcsetattr(fd, TCSANOW, savedattributes);
}

void SetNonCanonicalMode(int fd, struct termios *savedattributes){
    struct termios TermAttributes;
    char *name;

    if(!isatty(fd)){
        fprintf (stderr, "Not a terminal.\n");
        exit(0);
    }

    tcgetattr(fd, savedattributes);

    tcgetattr (fd, &TermAttributes);
    TermAttributes.c_lflag &= ~(ICANON | ECHO);
    TermAttributes.c_cc[VMIN] = 1;
    TermAttributes.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSAFLUSH, &TermAttributes);
}

string truncateDir(string inputStr){
    int start = inputStr.length();
    string truncd;

    for(int i = inputStr.length() - 1; inputStr[i] != '/'; i--)
        start = i;
    
    truncd += "/.../";
    for(int i = start; i< inputStr.length(); i++)
	truncd += inputStr[i];

    return truncd;
}

vector<string> argParse(string argString){
    stringstream argStream(argString);
    vector<string> argVector;
    string temp;
    while(argStream >> temp)
	argVector.push_back(temp);

    return argVector;


}

void executeCommand(vector <string> args){
    pid_t pid;
    int status;
    char** argv = new char*[args.size() + 1];  

    for(int i = 0; i< args.size(); i++)
        argv[i] = new char[args[i].length() + 1];

    for(int i = 0; i< args.size(); i++){
        for(int j = 0; j< args[i].length(); j++){
            argv[i][j] = args[i][j];
        }
        argv[i][args[i].length()] = '\0';
    }
    argv[args.size()] = NULL;


    if ((pid = fork()) < 0) {     /* fork a child process           */
	string error = "cannot fork child\n";
	write(1, error.c_str(), error.length());    	   
        exit(1);
     }
     else if (pid == 0) {          /* for the child process:         */
          if (execvp(*argv, argv) < 0) {     
	       string errorMsg = "Failed to execute " + args[0] + "\n";
               write(1, errorMsg.c_str(), errorMsg.length());
               exit(1);
          }//if invalid execution
     }
     else {                                  
          while (wait(&status) != pid)       /* wait for completion  */
               ;
     }



}

int main(int argc, char* argv[]){
    char userInput;
    string stringBuffer;
    vector <string> inputHistory;
    vector <string>::iterator it;

    vector<string> args; 
    
    char cwd[1000];
    int charCount = 0;

    struct termios SavedTermAttributes;
    SetNonCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
   
    struct stat stats;
    struct dirent *dirents;
 
    inputHistory.reserve(10);//this is done so we don't need to resize vector since that invalidates all the iterators in the vector
    it = inputHistory.begin();

    if(it == inputHistory.begin()){
        getcwd(cwd, sizeof(cwd));
        string temp = std::string(cwd);
	if(temp.length() >=16)
	    temp = truncateDir(temp);
	temp += "> ";
        write(STDOUT_FILENO, temp.c_str(), temp.length());

    }//if statement is used so console prints out pwd on the left once 

    while(1){

        int firstChar = stringBuffer.find_first_not_of(' ');

	read(STDIN_FILENO, &userInput, 1);
        if(isprint(userInput)){
                stringBuffer.push_back(userInput);
		it = inputHistory.end();
	        write(STDOUT_FILENO, &userInput, 1);
		charCount++;
        }//if input is a printable character 
//////////////////////////UP & DOWN/////////////////////////////////////////////
        else if(userInput == 0x1B){
                read(STDIN_FILENO, &userInput, 1);
                if(userInput == 0x5B){
                    read(STDIN_FILENO, &userInput, 1);
                    if(userInput == 0x41){
                        if(inputHistory.size() == 0){
			    write(STDOUT_FILENO, "\a", 1);
			}//NOP
                        else{
                            if(it == inputHistory.begin()){
				write(STDOUT_FILENO, "\a", 1);
			    }//NOP
                            else{
                                it--;
                                for(int i = 0; i< stringBuffer.size(); i++)
                                    write(STDOUT_FILENO, "\b \b", 3);
                                write(STDOUT_FILENO, (*it).c_str(), (*it).size());
                                stringBuffer = *it;
				charCount = stringBuffer.length();
                            }
                        }
                    }//if UP key

		    else if(userInput == 0x42){
			if(inputHistory.size()==0 || it==inputHistory.end()-1|| it == inputHistory.end()){
			     for(int i = 0; i< stringBuffer.size(); i++)
                                    write(STDOUT_FILENO, "\b \b", 3);
			     stringBuffer.clear();
			    if(it == inputHistory.end())
                                write(STDOUT_FILENO, "\a", 1);

			     it = inputHistory.end();
			     charCount = 0;
			}
			else{
                            it++;
                            for(int i = 0; i< stringBuffer.size(); i++)
                                write(STDOUT_FILENO, "\b \b", 3);
                             write(STDOUT_FILENO, (*it).c_str(), (*it).size());
                             stringBuffer = *it;
			     charCount = stringBuffer.length();
                            }
		    }//else if DOWN key
               }
            }//else if we're inputting a DIRECTION key

/////////////////////////////////////DELETE/////////////////////////////////////
	else if(userInput == 0x7F || userInput == 0x08){
	    if(charCount > 0){
		write(STDOUT_FILENO, "\b \b", 3);
		charCount--;
		stringBuffer.resize(stringBuffer.length()-1);
	    }   
	    else
		write(STDOUT_FILENO, "\a", 1);
	}
////////////////////////////////////////////////////////////////////////////////
        else{
            write(STDOUT_FILENO, &userInput,1);
	    if(userInput == 0x0A){

		args = argParse(stringBuffer);//parse user input arguments for use later

		if(inputHistory.size() == 10 && stringBuffer.length()!= 0){
                    if(it == inputHistory.begin()){
		        inputHistory.erase(inputHistory.begin());	    
			it = inputHistory.begin();
		    }
		    else
			inputHistory.erase(inputHistory.begin());
		}//if we're at max history size

                if(stringBuffer.length() != 0){
                    inputHistory.push_back(stringBuffer);
 //                   if(inputHistory.size() - 1 != 9)
                    it++; 
		    
                }

		if(stringBuffer[firstChar] == 'p' && stringBuffer[firstChar+1] == 'w' && stringBuffer[firstChar+2] == 'd'){
		    getcwd(cwd, sizeof(cwd));
		    string temp = std::string(cwd);			
		    if(isprint(stringBuffer[firstChar+3])) { //there's something after "pwd"
			if((stringBuffer.substr(firstChar+3,stringBuffer.length())).find_first_not_of(' ') != std::string::npos);
			else 
			{
                        write(STDOUT_FILENO, temp.c_str(), temp.length());
                        write(1, "\n", 1);
                        }
		    }
		    else {
			write(STDOUT_FILENO, temp.c_str(), temp.length());
			write(1, "\n", 1);	
		    }
		}//if PWD

		//if EXIT
		else if(stringBuffer[firstChar] == 'e' && stringBuffer[firstChar+1] == 'x' && stringBuffer[firstChar+2] == 'i'&& stringBuffer[firstChar+3] == 't'){
		    if(isprint(stringBuffer[firstChar+3])) { //there's something after "
			if((stringBuffer.substr(firstChar+4,stringBuffer.length())).find_first_not_of(' ') != std::string::npos);
                        else
                        {
			    break;
			}
		    }
		    else
		    	break; 
		} //end if EXIT



		//if CD
      		else if(stringBuffer[firstChar] == 'c' && stringBuffer[firstChar+1] == 'd' && stringBuffer[firstChar+2] == ' ' && isprint(stringBuffer[firstChar+3])) {
          	    string tempCD;
          	    char curDir[1024];
          	    string cdDir;
          
          	    getcwd(curDir, sizeof(curDir));
          	    cdDir = std::string(curDir); 
          
          	    for(int i = firstChar+3; stringBuffer[i] != '\0'; i++)
             		tempCD+= stringBuffer[i];
         	    if(tempCD.find_first_not_of(' ') != std::string::npos)
                        cdDir += "/" + tempCD.substr(tempCD.find_first_not_of(' '),tempCD.length());
         	    //cdDir += "/" + tempCD;
		    DIR *dir = opendir(cdDir.c_str());
		    if(!dir) {
			string tmp = "Error changing directory.";
			int strsize = tmp.length();
			write(1, tmp.c_str(), strsize);
			write(1, "\n", 1);
		    }
		    else
         	    	chdir(cdDir.c_str());  
		    closedir(dir);
     		}




		//if LS		
		else if(stringBuffer[firstChar] == 'l' && stringBuffer[firstChar+1] == 's' && stringBuffer[firstChar+2] == ' ' && isprint(stringBuffer[firstChar+3])){
		    int isValid = 1;
		    string tempLS;
		    char tempcwd[1024];
		    getcwd(tempcwd, sizeof(tempcwd));
		    string lsDir = std::string(tempcwd);
		    for(int i = firstChar+3; stringBuffer[i] != '\0'; i++)
		        tempLS += stringBuffer[i];
		    if(tempLS.find_first_not_of(' ') != std::string::npos)
			lsDir += "/" + tempLS.substr(tempLS.find_first_not_of(' '),tempLS.length());

		    DIR *dir = opendir(lsDir.c_str());
                    if(!dir) {
			string tmp = "Failed to open directory \"";
			string tmp2 = "\"";
			string outStr =  tmp + lsDir + tmp2;
			int strsize = outStr.length();
		        write(1, outStr.c_str(), strsize);
			write(1, "\n", 1);
			isValid = 0;
		    }
		    
		    while(isValid == 1 && (dirents = readdir(dir)) != NULL) {
		        stat(dirents->d_name, &stats);
		    write(1,(S_ISDIR(stats.st_mode)) ? "d" : "-", 1);
		    write(1,(stats.st_mode & S_IRUSR) ? "r" : "-", 1);
		    write(1,(stats.st_mode & S_IWUSR) ? "w" : "-", 1);
		    write(1,(stats.st_mode & S_IXUSR) ? "x" : "-", 1);
		    write(1,(stats.st_mode & S_IRGRP) ? "r" : "-", 1);
		    write(1,(stats.st_mode & S_IWGRP) ? "w" : "-", 1);
		    write(1,(stats.st_mode & S_IXGRP) ? "x" : "-", 1);
		    write(1,(stats.st_mode & S_IROTH) ? "r" : "-", 1);
		    write(1,(stats.st_mode & S_IWOTH) ? "w" : "-", 1);
		    write(1,(stats.st_mode & S_IXOTH) ? "x" : "-", 1);
		    write(1, " ", 1);
		    //write(1, ((lsDir.c_str() == std::string(tempcwd)) ? "."
		//		: lsDir.c_str()), lsDir.size());
			write(1, dirents->d_name, strlen(dirents->d_name));
		    write(1,"\n",1);
		    }
		    closedir(dir);
		}

		else if(stringBuffer[firstChar] == 'l' && stringBuffer[firstChar+1] == 's' && stringBuffer[firstChar+2] == '\0'){
                    char tempcwd[1024];
                    getcwd(tempcwd, sizeof(tempcwd));
                    DIR *currentDir = opendir(tempcwd);
		    while((dirents = readdir(currentDir)) != NULL) {
                        stat(dirents->d_name, &stats);
		    write(1,(S_ISDIR(stats.st_mode)) ? "d" : "-", 1);
                    write(1,(stats.st_mode & S_IRUSR) ? "r" : "-", 1);
                    write(1,(stats.st_mode & S_IWUSR) ? "w" : "-", 1);
                    write(1,(stats.st_mode & S_IXUSR) ? "x" : "-", 1);
                    write(1,(stats.st_mode & S_IRGRP) ? "r" : "-", 1);
                    write(1,(stats.st_mode & S_IWGRP) ? "w" : "-", 1);
                    write(1,(stats.st_mode & S_IXGRP) ? "x" : "-", 1);
                    write(1,(stats.st_mode & S_IROTH) ? "r" : "-", 1);
                    write(1,(stats.st_mode & S_IWOTH) ? "w" : "-", 1);
                    write(1,(stats.st_mode & S_IXOTH) ? "x" : "-", 1);
                    write(1, " ", 1);
		    write(1, dirents->d_name, strlen(dirents->d_name));
                    write(1,"\n",1);
		    }
		    closedir(currentDir);
		} //end if LS

		

/*
		if(stringBuffer.length() != 0){
              	    inputHistory.push_back(stringBuffer);
		    if(inputHistory.size() - 1 != 9)
		        it++;
		}
*/

                else if(stringBuffer == "history"){
                    for(int i = 0; i <inputHistory.size(); i++){
			ostringstream sStream;
                        sStream<<i;
			string temp = sStream.str()+" " +inputHistory[i] + "\n";
			write(STDOUT_FILENO, temp.c_str(), temp.length());
		    }
		}

		else{//else it's not a program defined command
		    executeCommand(args);



		}
		char currDir[1024];

		getcwd(currDir, sizeof(currDir));
		string showDir = std::string(currDir);
		if(showDir.length() >=16)
		    showDir = truncateDir(showDir);
		showDir += "> ";
		write(STDOUT_FILENO, showDir.c_str(), showDir.length());

		stringBuffer.clear();
		charCount = 0;

	    }//if ENTER 
        }//else 
    }






    ResetCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
    return 0;

}

