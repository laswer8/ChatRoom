local bloomkey=KEYS[1]	--布隆过滤器key
local findkey=KEYS[2]	--查找key
local ttl=tonumber(ARGV[1])	--过期时间

local NULL_mark = '__NULL__'

-- 查询布隆是否存在
if redis.call('BF.EXISTS',bloomkey,findkey) == 0 then
	-- 不存在返回-1代表不存在数据
	return {-1,''}
else
-- 可能存在，继续下一步
	local res = redis.call("GET",findkey)
	if res then
		if res == NULL_mark then
			-- 布隆过滤器缓存的空值
			redis.call('PEXPIRE', findkey, ttl)
			return {-1,''}
		else
			-- 成功找到，续期，返回1代表成功
			redis.call('PEXPIRE', findkey, ttl)
			return {1,res}
		end
	else
	-- 0代表不存在缓存，需要前往mysql查找
		return {0,''}
	end
end