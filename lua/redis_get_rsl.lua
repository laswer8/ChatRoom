-- 获取最近会话记录rsl
-- KEYS[1]: UID（作为rsl的key的前缀）
-- ARGV[1]: TTL（过期时间，单位：秒）

-- 根据ID生成各个key的名称
local uid = KEYS[1]
local ttl = tonumber(ARGV[1])

local list_key = "rsl:list:user:" .. uid      -- rsl列表key

local counter_key = "" -- 未读计数器key
local msg_key = "" -- 单条消息key

-- 1. 获取最近会话列表（最多20条，最新的在最前面）
local ids = redis.call('LRANGE', list_key, 0,-1)
if #ids == 0 then
	return {}
end
-- 2. 重置过期时间
redis.call('EXPIRE', list_key, ttl)
-- 3. 组装最近会话记录
local rsl_list = {}
for i, session_id in ipairs(ids) do
	counter_key = "rsl:weidu:user:" .. uid .. ":to:"..session_id
	msg_key = "rsl:last_msg:user:" .. uid .. ":to:"..session_id
	local weidu = redis.call('GET', counter_key) or "0"
	local last_msg = redis.call('GET', msg_key) or ""
	rsl_list[i] = {
		session_id,
		weidu,
		last_msg
	}
	-- 重置计数器
	redis.call('SET', counter_key, 0,"EX",ttl)
	-- 重置过期时间
	redis.call('EXPIRE', msg_key, ttl)
end
return rsl_list