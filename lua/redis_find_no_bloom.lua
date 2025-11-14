local findkey=KEYS[1]	--查找key
local ttl=tonumber(ARGV[1])	--过期时间

local res = redis.call("GET",findkey)
if res then
	if res == NULL_mark then
		redis.call('PEXPIRE', findkey, ttl)
		return {1,res}
	end
else
-- 0代表不存在缓存，需要前往mysql查找
	return {0,''}
end