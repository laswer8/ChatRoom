#ifndef SSL_SERVICE_HPP_
#define SSL_SERVICE_HPP_

#include "HeadFile.h"

#define WORK_FACTOR_ 12   //默认成本因子
#define PRIVATE_KEY_PATH  "./key/server_pri_key.pem"
#define PUBLIC_KEY_PATH  "./key/server_pub_key.pem"


namespace SSLService {
    enum verify_status {
        OK = 0, //成功
        EXPIRE,  //过期
        FAILED   //失败
    };

    inline std::string read_file(const std::string& filepath) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + filepath);
        }
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    inline string generatorUUID() {
        thread_local boost::uuids::random_generator uuid_v4;
        return boost::uuids::to_string(uuid_v4());
    }


    /// bcrypt算法加密密码
    /// @param str 密码
    /// @param encode 输出
    /// @param work_factor 成本因子，值与安全性成正比，与性能成反比，默认12
    /// @return 加密成功返回true
    inline bool bcryptEncode(const string& str,string& encode,int&& work_factor = WORK_FACTOR_) {
        try {
            if (str.empty())return false;
            if (work_factor < 4 || work_factor > 31) {
                //成本因子超出范围，调整为默认值
                work_factor = WORK_FACTOR_;
            }
            encode = BCrypt::generateHash(str,work_factor);
            return true;
        }catch (exception& e) {
            LOG_INFO<<e.what();
            return false;
        }
    }

    /// bcrypt算法验证密码
    /// @param str 密码
    /// @param encode 加密后的密码
    /// @return 相同返回true
    inline bool bcryptVartify(const string& str,const string& encode) {
        try {
            auto start = std::chrono::high_resolution_clock::now();
            auto res = BCrypt::validatePassword(str,encode);
            std::cout << "bcrypt解密验证函数执行时间: " <<
                                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start).count()
                            << " 毫秒" << std::endl;
            return res;
        }catch (exception& e) {
            LOG_INFO<<e.what();
            return false;
        }
    }

    inline string getJWT(const string& issuer,const string& subject,const unordered_map<string,jwt::claim>& payload,
        const chrono::time_point<chrono::system_clock, chrono::system_clock::duration>& expires,
        const  chrono::time_point<chrono::system_clock, chrono::system_clock::duration>& issued_at,
        const string& jti = "",
        const string& private_key_name = PRIVATE_KEY_PATH,
        const string& public_key_name = PUBLIC_KEY_PATH) {
        static unordered_map<string,string> key_cache;
        static mutex m;
        try {

            string key_content = read_file(private_key_name);
            string public_key_content = read_file(public_key_name);
            // {
            //     lock_guard<mutex> l(m);
            //     if (key_cache.count(private_key_name) == 0) {
            //         key_content = read_file(private_key_name);
            //         key_cache[private_key_name] = key_content;
            //     }else {
            //         key_content = key_cache[private_key_name];
            //     }
            //     if (key_cache.count(public_key_name) == 0) {
            //         public_key_content = read_file(public_key_name);
            //         key_cache[public_key_name] = public_key_content;
            //     }else {
            //         public_key_content = key_cache[public_key_name];
            //     }
            // }

            auto builder = jwt::create().set_type("JWT").
            set_issuer(issuer).set_subject(subject).set_expires_at(expires).set_issued_at(issued_at);
            if (!jti.empty())
                builder.set_id(jti);
            for (const auto& [key,value]: payload) {
                builder.set_payload_claim(key,value);
            }
            return builder.sign(jwt::algorithm::rs256(public_key_content,key_content,"",""));
        }catch (exception& e) {
            LOG_INFO<<e.what();
            return {};
        }

    }

    inline verify_status verifyJWT(const string& token,const string& issuer,const unordered_map<string,jwt::claim>& payload,
        const string& subject = "",
        const string& jti = "",
        const uint64_t& leeway_seconds = 120,
        const string& private_key_name = PRIVATE_KEY_PATH,
        const string& public_key_name = PUBLIC_KEY_PATH) {
        static unordered_map<string,string> key_cache;   //公钥缓存
        static mutex mx;
        try {
            string key_content = read_file(private_key_name);
            string public_key_content = read_file(public_key_name);
            {
                lock_guard<mutex> l(mx);
                if (key_cache.count(private_key_name) == 0) {
                    key_content = read_file(private_key_name);
                    key_cache[private_key_name] = key_content;
                }else {
                    key_content = key_cache[private_key_name];
                }
                if (key_cache.count(public_key_name) == 0) {
                    public_key_content = read_file(public_key_name);
                    key_cache[public_key_name] = public_key_content;
                }else {
                    public_key_content = key_cache[public_key_name];
                }
            }

            auto decode = jwt::decode(token);
            auto verifier = jwt::verify().allow_algorithm(
                jwt::algorithm::rs256(public_key_content,key_content,"",""))
            .with_issuer(issuer).with_type("JWT").leeway(leeway_seconds);
            if (!jti.empty()) {
                verifier.with_id(jti);
            }
            if (!subject.empty())
                verifier.with_subject(subject);
            for (const auto& [key,value]:payload) {
                verifier.with_claim(key,value);
            }
            verifier.verify(decode);
            return verify_status::OK;

        }catch (jwt::error::token_verification_exception& e) {
            if (e.code() == jwt::error::token_verification_error::token_expired) {
                //令牌过期
                return verify_status::EXPIRE;
            }
            LOG_INFO<<"验证失败："<<e.what();
            return verify_status::FAILED;
        }catch (exception& e) {
            LOG_INFO<<e.what();
            return verify_status::FAILED;
        }
    }

};

#endif