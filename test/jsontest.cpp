#include "../include/server/HeadFile.h"

void test0_6(){
    //1. 由value创建JSON对象
    //      赋值构造
    json j1;
    j1["name"]="li dong";     //字符串
    j1["age"]=20;                       //整数
    j1["man"]=true;                 //bool
    j1["array"]={"value1","value2","value3"}; //数组
    j1["Class"]["key1"]="this is a class member`s value";   //对象中的元素赋值
    j1["wifi"]={{"name","CMCC"},{"Password","13979548140"}};//对象

    //      直接构造
    json j2={
        {"name","li dong"},
        {"age",20},
        {"man",true},
        {"array",{"value1","value2","value3"}},
        {"Class",{{"key1","this is a class member`s value"}}},
        {"wifi",{{"name","CMCC"},{"Password","13979548140"}}}
    };
    cout<<"j1: "<<j1<<endl;
    cout<<"j2: "<<j2<<endl;
    //由JSON对象获得value

    //key:value类型：获取key为"name"对应的Value,并强转为string类型
    auto name = j2["name"].get<string>();
    cout<<"name = "<<name<<"   "<<"Type = "<<typeid(name).name()<<endl;

    //数组类型：使用对应下标，从0开始
    string str;
    for(int i= 0;i<3;i++){
        str=j2["array"][i].get<string>();
        //可以像vector一样使用at()
        // str=j2["array"].at(i).get<string>();
        cout<<"array["<<i<<"] = "<<str<<endl;
    }
    
    //对象类型：像使用多维数组一样获取
    string key = j2.at("Class").at("key1").get<string>();
    cout<<"value="<<key<<endl;
    string wifiname = j2["wifi"]["name"].get<string>();
    string wifipassword = j2["wifi"]["Password"].get<string>();
    cout<<"wifi: "<<wifiname<<"----"<<wifipassword<<endl;

    //3. STL风格操作
    //vector
    json jsonarray = {"dog","cat"};//数组类型json对象：[dog , cat]
    jsonarray.push_back("cow");
    jsonarray.emplace_back("pig");
    cout<<"jsonarray: "<<jsonarray<<endl;
    //类型判断: 是数组且非空
    if(jsonarray.is_array() && !jsonarray.empty()){
        //获取元素数量
        cout<<"member num: "<<jsonarray.size()<<endl;
        cout<<"jsonarray[size-1]="<<jsonarray[jsonarray.size()-1].get<string>()<<endl;
    }
    //map
    json jsonmap={{"kind","dog"},{"name","xixi"}};
    jsonmap.push_back({"wegiht",80});//添加
    jsonmap.erase("kind");//删除
    cout<<"dog: "<<jsonmap<<endl;
    jsonmap["wegiht"]=30;//修改
    //判断元素是否存在
    if(jsonmap.contains("name")){
        cout<<"方式一"<<endl;
    }
    if(jsonmap.count("wegiht")>0){
        cout<<"方式二"<<endl;
    }
    if(jsonmap.find("name") != jsonmap.end()){
        cout<<"方式三"<<endl;
    }
    //遍历
    for(auto s:jsonmap.items()){
        cout<<"方式一"<<s.key()<<":"<<s.value()<<endl;
    }
    for(auto i = jsonmap.begin();i!=jsonmap.end();i++){
        cout<<"方式二"<<i.key()<<":"<<i.value()<<endl;
    }
    string s = jsonmap.at("name").get<string>();

    //4. 反序列化json(string-->json)
    //方式一：通过_json方式，直接构造
    json tempjson1 = "{\"name\":\"li dong\",\"age\":18,\"man\":true}"_json;
    //方式二：使用R()原生字符串，避免多次使用转意字符，结合parse()函数解析成json
    string jsonstring = R"({"name":"li dong","age":18,"man":true})";
    json tempjson2 = json::parse(jsonstring);
    cout<<"方式一："<<tempjson1<<endl;
    cout<<"方式二："<<tempjson2<<endl;
    
    //5.序列化json(json-->string)
    //dump()成员函数，可选参数为size_t,缩进空格数，且添加换行符
    string jsonstr1 = tempjson1.dump();
    string jsonstr2 = tempjson2.dump(0);
    cout<<"没有空格："<<jsonstr1<<endl<<"有空格："<<jsonstr2<<endl;

    //6. JSON对象与JSON文件之间的输入输出
    //JSON文件读取
    ifstream in("./oldfile.json");//打开一个文件，并绑定到输入流in上
    json jsonfile={"aaa","bbb"};    //创建一个JSON对象，其初值会被覆盖
    in>>jsonfile;//读取文件内容写入json对象
    in.close();//关闭文件
    jsonfile["jj lenth"]=18;//添加元素
    cout<<jsonfile.dump(0)<<endl;

    //JSON文件输入
    ofstream out("./oldfile.json");//绑定到输出流
    jsonfile["age"]=20;//修改
    out<<setw(4)<<jsonfile;//setw设置打印空格数，并换行(大于0时才会换行)，将jsonfile重新写入文件中
    out.close();//一定要记得关文件

}


int main(){
    test0_6();
    return 0;
}