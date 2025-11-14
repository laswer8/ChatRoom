local key=KEYS[1]
local value=ARGV[1]
local ttl=ARGV[2]

if redis.call('SET',key,value,'PX',ttl,'NX') == 0 then
	return 0
end
return 1