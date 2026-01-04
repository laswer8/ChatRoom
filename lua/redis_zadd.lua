-- KEYS[1] = 离线消息队列键名
-- ARGV[1] = 消息内容 (原始字符串)
-- ARGV[2] = 消息序号 (客户端维护的排序依据)
-- ARGV[3] = TTL (过期时间，秒，0表示不过期)
local ttl = tonumber(ARGV[3])
local msg_no = tonumber(ARGV[2])
-- 存储上限
local max_size = 100
-- 一次删最早的20条
local batch_del = 20
local size = redis.call('ZCARD',KEYS[1])
if size >= max_size then
	local elements = redis.call('ZRANGE',KEYS[1],0,batch_del-1)
	if #elements > 0 then
		redis.call('ZREM',KEYS[1],unpack(elements))
	end
end
-- 添加到离线队列
if redis.call('ZADD', KEYS[1], msg_no, ARGV[1]) <= 0 then
	return 0
end
if ttl <= 0 or redis.call('EXPIRE', KEYS[1], ttl) <= 0 then
	return 0
end
return 1
