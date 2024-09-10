#include<iostream>
#include<string>
using namespace std;

void input(string& s){
    string str;
    cout<<"鱼杂十大";
    getline(cin,str,'\n');
    for(char& c:str){
        if(c == '\n'){
            break;
        }
        if(c == ' '){
            s += '\x01';
        }else if(c == '\"'){
            s += "\\\"";
        }else if(c == '\\'){
            s += "\\\\";
        }else
            s += c;
    }
}

int main(){
    string a,b,c;
    input(a);
    cout<<a<<endl;
    input(b);
    cout<<b<<endl;
    input(c);
    cout<<c<<endl;
    return 0;
}