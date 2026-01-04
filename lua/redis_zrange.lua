-- KEYS[1] = 离线消息队列键名 (e.g., "offline:msgs:123456")
-- ARGV[1] = 开始位置 (start, 0表示第一条)
-- ARGV[2] = 结束位置 (stop, -1表示最后一条)
-- ARGV[3] = 是否删除 (1=获取并删除, 0=仅获取)
local start = tonumber(ARGV[1])
local stop = tonumber(ARGV[2])
-- 1. 获取消息列表 (按序号升序)
local message_list = redis.call('ZRANGE', KEYS[1], start, stop)

-- 2. 如果没有消息，直接返回空数组
if #message_list == 0 then
	return {}
end

-- 3. 准备获取消息内容的pipeline
local messages = {}
for i, message in ipairs(message_list) do
	messages[i] = message
end

-- 4. 如果需要删除，执行删除操作
if tonumber(ARGV[3]) == 1 then
	-- 4.2 从队列中移除消息ID
	for i, message in ipairs(message_list) do
		redis.call('ZREM', KEYS[1], message)
	end
end

-- 5. 返回消息列表
return messages

--1. 用户登录时不应该直接获取离线消息
--2. 离线消息应该在用户点击会话时获取
--3. 离线消息的存储设计难点：
--	1. redis为主存储，MySQL用于恢复业务
--	2. 离线消息按会话存储，类型为zset
--	3. 离线消息获取即全部删除，过期也删除
--	4. 离线消息与最近会话记录有关
--4. 历史记录应存储在MySQL为主，按会话存储，类型为zset
--5. 历史记录存储要点：
--	1. 历史记录不应该在服务端修改
--	2. 历史记录由客户端维护，定期提交服务端存储
--	3. 历史记录按范围获取，不删除
--	4. 历史记录可以使用缓存
--	5. 与最近会话记录无关
--
--6. 最近会话记录要点：
--	1. 只存储redis
--	2. 不是单一消息
--	3. 多string组合
--	4. 有一定消息需要服务端更新
--7. 离线消息与最近会话记录关系：
--	1. 最近会话记录内容{[{"ID":0,"weidu":0,"last_msg":""}]}
--	2. weidu（未读数）直接与离线消息数量有关
--	3. last_msg(最新消息)也与最新发送的离线消息有关
