local jti=KEYS[1]	--查找key

-- 查询jti是否存在
local res = redis.call("GET",jti)
if res then
	-- 存在返回1并删除
	redis.call("DEL",jti)
	return 1
else
-- 0表示不存在
	return 0
end