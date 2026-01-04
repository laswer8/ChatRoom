-- 最近会话记录更新rsl操作
-- KEYS[1]: UID（作为rsl的key的前缀）
-- KEYS[2]: ID 会话ID
-- ARGV[1]: 消息内容
-- ARGV[2]: TTL（过期时间，单位：秒）

-- 根据ID生成各个key的名称
local uid = KEYS[1]
local session_id = KEYS[2]
local message = ARGV[1]
local ttl = tonumber(ARGV[2])

local list_key = "rsl:list:user:" .. uid      -- rsl列表key
local counter_key = "rsl:weidu:user:" .. uid .. ":to:"..session_id -- 未读计数器key
local msg_key = "rsl:last_msg:user:" .. uid .. ":to:"..session_id    -- 单条消息key

-- 1. 更新计数器（递增1）
redis.call('INCR', counter_key)

-- 2. 维护会话列表（最多20条，最新的在最前面）
-- 去重
redis.call('LREM', list_key, 1, session_id)
-- 将新消息添加到列表左侧（头部）
redis.call('LPUSH', list_key, session_id)

-- 如果列表长度超过20，删除最旧的消息（从右侧删除）
local list_len = redis.call('LLEN', list_key)
if list_len > 20 then
	redis.call('RPOP', list_key)  -- 删除最旧的一条
end

-- 3. 更新单条消息（覆盖旧的消息）
redis.call('SET', msg_key, message)

-- 4. 为所有key设置统一的TTL
redis.call('EXPIRE', list_key, ttl)
redis.call('EXPIRE', counter_key, ttl)
redis.call('EXPIRE', msg_key, ttl)