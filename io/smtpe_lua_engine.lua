dofile '/root/table_show.lua'

-- Lua Socket
require 'socket'

-- Connect to redis
require 'redis'
local redis = Redis.connect() 

-- JSON
require 'json'

-- OSBF/Lua
local osbf = require 'osbf'

-- kvm lookups
dofile '/root/kvm.lua'

--[[

Completed:
:: Multi-line banner (optional)
:: Extra spaces
:: Extra parameters when not in ESMTP mode
:: DNS BL/WL/GL
:: Quarantining
:: HELO schizophrenic short-term
:: Early-talker detection
:: Pipeline detection
:: SMTP command case
:: EHLO/HELO tests
:: Call-aheads
:: Addressbook AWL
:: Greylisting
:: One RCPT per NULL
:: Strict Relaying
:: Client is MX
:: spamd/clamd
:: HELO schizophrenic long-term
:: URIBL
:: Short URL resolution
:: Min/Max Headers
:: Header modifications
:: Message Size
:: Relaying
:: Forwarding
:: Access-map whitelist/blacklist

Things to implement:
:: Preferences
:: Rate Limiting
:: BCC-style functions
:: Click whitelisting
:: Electronic Watermarks (EMEW)
:: OSBF-Lua
:: SPF
:: Filename/Filetype rules
:: ZIP/RAR Filename rules
:: Charset rules
:: Stats
:: Access-map BODY tags

Other TODO:
:: Test IPv6 support
:: Test framework

]]--

local conf = {
	['quarantine_path'] = '/var/spool/smtpe/quarantine',
	['greylist_delay'] = 900,
	['greylist_pass_ttl'] = (86400*40),  -- 40 days
	['greylist_key_ttl'] = 90000,  -- 25 hours
	['abook_ttl'] = (86400*40), -- 40 days
	['helo_ttl'] = (86400*40), -- 40 days
	['spamd_score_reject'] = 999,
	['uri_max_limit'] = 10,
	['dns_blacklists'] = {
		'zen.spamhaus.org',
		'bl.spamcop.net',
		'bb.barracudacentral.com',
	},
	['dns_yellowlists'] = {
		'list.dnswl.org',
	},
	['dns_whitelists'] = {
	},
	['uribl_domains'] = {
		'black.uribl.com',
		'multi.surbl.org',
	},
	['uribl_hosts'] = {
		'dbl.spamhaus.org',
		'fresh15.spameatingmonkey.net',
	},
	['spamd_hosts'] = {
		'127.0.0.1',
	},
	['clamd_hosts'] = {
		'127.0.0.1',
	},
	['osbf_dbset'] = {
		classes = {'/tmp/osbf_nonspamdb.cfc', '/tmp/osbf_spamdb.cfc'},
		ncfs = 1,
		delimiters = ""
	},
	['message_max_size'] = (1*1024*1024), -- 25Mb
	['debug'] = 1,
	['rejectables'] = {
		['enable_ehlo'] = false,
		['dns_bl'] = true,
		['no_rdns'] = false,
		['rdns_invalid_tld'] = true,
		['strict_helo'] = true,
		['helo_schizophrenic'] = true,
		['extra_params'] = true,
		['mail_mx_required'] = true,
		['one_domain_per_conn'] = true,
		['one_rcpt_per_null'] = true,
		['strict_relay'] = true,
		['greylisting'] = false,
	}
}

local domains = {
	['vm2.fsl.com'] = { '127.0.0.1:26' },
}

local conn = nil
function init_conn_table()
	conn = nil
	conn = {
		id = nil,
		ptrs = {},
		host_id = nil,
		helo = nil,
		ehlo_no_helo = false,
		unknown_count = 0,
		noop_count = 0,
		rset_count = 0,
		vrfy_count = 0,
		expn_count = 0,
		txn_count = 0,
		clean_quit = false,
		timed_out = false,
		flags = {},
		rejectables = {},
		reply_stats = { total = 0 },
		start_time = nil,
		last_error = nil,
		last_reply = nil,
		bytes_in = 0,
		bytes_out = 0,
		msgs_accepted = 0,
		msgs_tempfail = 0,
		msgs_rejected = 0,
		rcpts_accepted = 0,
		rcpts_tempfail = 0,
		rcpts_rejected = 0,
		quarantine = false,
		quarantine_type = nil,
	}
	conn.start_time = socket.gettime()
	conn.id = client.id_sess
	debug('init: \'conn\' table initialized')
end

local txn = nil
function init_txn_table()
	txn = nil
	txn = {
		id = nil,
		date = nil,
		sender = nil,
		rcpts_accepted = {},
		rcpts_tempfail = {},
		rcpts_rejected = {},
		rcpt_domains = {},
		prefs = 'DEFAULT',
		headers = {
		},
		size = 0,
		quarantine = false,
		quarantine_type = nil,
		flags = {},
		rejectables = {},
		dot_rejectables = {},
		msg_needs_tagging = false,
		spamd_score = nil,
		spamd_rules_hit = {},
	}
	-- Add date in YYYYMMDD format to transaction table
	-- This is used when messages are quarantined.
	local date = os.date('!*t')  -- NOTE: UTC
	txn.date = date.year .. string.format('%02d', date.month) .. string.format('%02d', date.day)
	txn.id = client.id_trans
	debug('init: \'txn\' table initialized')
end

--print ('CPU count: '..util.cpucount())
--print (table.show(util.getloadavg(),'loadavg'))

function hook.accept(ip, ptr)
	debug(string.format('hook.accept: ip=%s, ptr=%s', ip, ptr))

	-- rate/concurrency limits here

	-- Initialisation
	init_conn_table()

	-- Access map lookups
	local access = accessConnect(ip, ptr)
	if access and access.action then
		if access.action == 'OK' then
			conn.flags['white'] = true
		elseif access.action == 'CONTENT' then
			conn.flags['content'] = true
		elseif access.action == 'REJECT' then
			add_rejectable(conn.rejectables, {
				name = 'black',
				output = ternary(access.action_text, access.action_text, string.format('550 host %s [%s] black listed', ptr, ip))
			})
		elseif access.action == 'IREJECT' then
			return true, ternary(access.action_text, access.action_text, string.format('550 host %s [%s] black listed', ptr, ip))
		elseif access.action == 'DISCARD' then
			conn.flags['discard'] = true
		elseif access.action == 'TAG' then
			conn.flags['tag'] = true
		elseif access.action == 'SAVE' then
			conn.flags['save'] = true
			conn.quarantine = true
			if access.action_text then
				conn.quarantine_type = access.action_text
			else
				conn.quarantine_type = 'save'
			end
		elseif access.action == 'TRAP' then
			conn.flags['trap'] = true
			conn.quarantine = true
			if access.action_text then
				conn.quarantine_type = access.action_text
			else
				conn.quarantine_type = 'trap'
			end
		else
			syslog.error(string.format('access-map unknown action \'%s\'', access.action))
		end
	end

	-- Earlytalker checks
	if client.is_pipelining then
		debug('hook.accept: earlytalker detected!')
		add_rejectable(conn.rejectables, {
			name = 'earlytalker',
			output = '550 5.3.3 pipelining not allowed'
		})
	end

	-- PTR record checks
	-- TODO: CNAME/DNAME resolution
	-- Skip all PTR checks for Loopback and LAN IPs
	if not net.is_ip_reserved(ip, (net.is_ip.LAN + net.is_ip.LOCAL)) then
		-- Re-do the PTR lookup here as there might be multiple PTRs
		dns.open()
		dns.query(dns.class.IN, dns.type.PTR, ip)
		local answers = dns.wait(1)
		dns.reset()
		local result = answers['IN,PTR,'..net.reverse_ip(ip,1)]
		if not result.answer then
			-- Check rcode
			if result.rcode ~= dns.rcode.OK and 
			   result.rcode ~= dns.rcode.NXDOMAIN and
			   result.rcode ~= dns.rcode.NOERROR
			then
				add_rejectable(conn.rejectables, {
					name = 'no_rdns',
					output = string.format('450 Error resolving reverse DNS name for host [%s] (%s)', ip, dns.rcodename(result.rcode))
				})
			else
				add_rejectable(conn.rejectables, {
					name = 'no_rdns',
					output = string.format('550 Host [%s] has no reverse DNS name', ip)
				})
			end 
		else 
			for i, answer in ipairs(result.answer) do
				-- TODO: check answer.type == PTR
				answer.value = answer.value:lower()
				table.insert(conn.ptrs,answer.value)
				-- is PTR valid TLD?
				if not net.has_valid_tld(answer.value) then
					add_rejectable(conn.rejectables, {
						name = 'rdns_invalid_tld',
						output = string.format('550 Reverse DNS name for [%s] does not have a valid TLD (%s)', ip, answer.value)
					})
				end 
				-- IP in name?
				if net.is_ipv4_in_name(ip, answer.value) > 0 then
					conn.flags['ip_in_rdns'] = true
				end
				-- FCrDNS?
				dns.query(dns.class.IN, dns.type.A, answer.value)
				local fcrdns_answers = dns.wait(1)
				if fcrdns_answers and fcrdns_answers['IN,A,'..answer.value].answer and
				   fcrdns_answers['IN,A,'..answer.value].answer[1].value == ip 
				then
						conn.flags['fcrdns'] = true
				end
				dns.reset()
				local fcrdns_answers = nil
				-- Multiple PTRs
				if #conn.ptrs > 1 then
					if hostname_to_domain(answer.value) ~= hostname_to_domain(conn.ptrs[1]) then
						conn.flags['rdns_multidomain'] = true
					else
						conn.flags['rdns_multiple'] = true
					end
				end
			end
			local result = nil
			dns.reset()
		end
		dns.close()
	end

	-- Calculate the host ID for use with greylisting
	-- TODO: consider HELO name
	if conn.rejectables['no_rdns'] or 
	   conn.rejectables['rdns_lookup_error'] or
	   conn.flags['rdns_multidomain'] or 
	   conn.rejectables['rdns_invalid_tld'] or
	   conn.flags['ip_in_rdns'] or
	   not conn.flags['fcrdns']
	then
		conn.host_id = ip
	else
		if #conn.ptrs > 1 then
			-- Domain must be common; use it
			conn.host_id = hostname_to_domain(conn.ptrs[1])
		else
			if conn.ptrs and #conn.ptrs > 0 then
				_,_,conn.host_id = string.find(conn.ptrs[1], '^[^%.]+%.([%w%.]+)%.$') 
			end
			-- Fall-back to IP if no match
			if not conn.host_id then
				conn.host_id = ip
			end
		end
	end

	debug('greylisting: host_id='..conn.host_id)

	-- Check to see if this host has passed greylisting
	if redis:get('grey-pass:'..conn.host_id) then
		conn.flags['passed_greylist'] = true
	end

	-- TODO: lookup conn.ptrs in URI/Domain blacklists.

	-- DNS list lookups
	if not (conn.flags['white'] or conn.flags['content']) then 
		-- White lists (trusted, skip all checks except AV)
		local wl = dns_list_lookup_ip(ip, conf.dns_whitelists)
		if wl and #wl > 0 then conn.flags['dns_wl'] = true end
		-- Yellow list (skip blacklists, greylist etc.)
		if not conn.flags['dns_wl'] then
			local yl = dns_list_lookup_ip(ip, conf.dns_yellowlists)
			if yl and #yl > 0 then conn.flags['dns_yl'] = true end
		end
		-- Black lists
		if not (conn.flags['dns_wl'] or conn.flags['dns_yl']) then
			local bl = dns_list_lookup_ip(ip, conf.dns_blacklists)
			if bl and #bl > 0 then
				for i, list in ipairs(bl) do
					add_rejectable(conn.rejectables, {
						name = 'dns_bl',
						sub_test = list,
						output = string.format('550 5.7.0 Host %s [%s] black listed by %s', ptr, ip, list)
					})
				end
			end
		end
	end

	-- TODO: Have we seen this host before (rep=?)

	-- Is this host allowed to relay?
	local route = nil
	if conn.flags['fcrdns'] then
		-- Only allow relaying based on PTR if it is forward confirmed
		route = routeIsRelay(ip, ptr)
	else
		route = routeIsRelay(ip)
	end
	if route and (route.v and string.find(route.v, 'RELAY')) then
		conn.flags['relay'] = true;
	end

	-- Log point
	syslog.info(string.format('start i=%s p="%s" f="%s"', ip, ptr, table_concat_keys(conn.flags)))

	-- SMTP Banner
	local banner
	-- Send a single line banner if the host is known good
	banner = string.format('220 %s ESMTP (%s)', smtpe.host, client.id_sess)
	if net.is_ip_reserved(ip, (net.is_ip.LAN + net.is_ip.LOCAL))
	or conn.flags['white'] or conn.flags['content'] or conn.flags['relay'] 
	or conn.flags['dns_wl']	or conn.flags['dns_yl'] or conn.flags['passed_greylist']
	then
		return banner
	end
	-- Send a multi-line banner to attempt to confuse some SMTP clients
	-- We also check for input between each line to catch earlytalkers.
	banner = string.format(
		[[220-%s ESMTP (%s)]].."\r\n"..
		[[220 Spam is not welcome, accepted or tolerated here. You have been warned...]].."\r\n",
		smtpe.host, client.id_sess
	)
	return banner
end

function hook.reset()
	debug('in hook.reset');

	----------------
	-- Quarantine --
	----------------
	-- This is done in hook.reset to guarantee that any changes to
	-- the tranport file have been completed by the content fiters.
	-- This hook is run just prior to the file being unlinked.
	-- TODO: check to make sure that client.msg_file is not zero length
	local quarantine = false
	local quarantine_type = nil
	if conn and conn.quarantine then
		quarantine = conn.quarantine
		if conn.quarantine_type then
			quarantine_type = conn.quarantine_type
		end
	end
	if txn and txn.quarantine then
		quarantine = txn.quarantine
		if txn.quarantine_type then
			quarantine_type = txn.quarantine_type
		end
	end
	if quarantine and client.msg_file then
		local new_path = conf.quarantine_path .. '/' .. txn.date
		if quarantine_type then
			new_path = new_path .. '/' .. quarantine_type
		end
		-- Create path if necessary
		local bool = util.mkpath(new_path)
		-- Add in transaction ID
		new_path = new_path .. '/' .. client.id_trans;
		local success, err = os.rename(client.msg_file, new_path);
		debug(string.format('quarantined message: path=%s', new_path))
		if success then
			-- TODO: store pointer in datastore?
		else
			syslog.error(string.format('error quarantining file: %s', err))
		end
	end

	init_txn_table()
end

function hook.ehlo(arg)
	debug('hook.ehlo: arg='..arg)

	-- Check for pipelining
	check_for_pipelining(client.is_pipelining, false)
	-- Check command case
	check_command_case(client.input)
	-- Store HELO argument
	conn.helo = arg

	-- TODO: Access Map lookup

	-- Allow localhost, private IPs, relays and known servers
	if net.is_ip_reserved(client.address, (net.is_ip.LAN + net.is_ip.LOCAL))
	or conn.flags['white'] or conn.flags['dns_wl'] 
	or conn.flags['content'] or conn.flags['dns_yl']
	or conn.flags['relay'] or conn.flags['passed_greylist']
	then
		conn.flags['esmtp'] = true
	else
		-- Track hosts that QUIT if we reject EHLO
		conn.ehlo_no_helo = true
		return '502 5.5.1 EHLO not allowed from your host'
	end
end

function verify_helo(arg)
	-- Check for IPv6 address literal first as it will not contain dots
	local _,_,ip = string.find(arg:lower(), '^ipv6:%[([a-z0-9:]+)%]$')
	if ip then
		-- TODO: improve syntax checking here (e.g. literal matches address)
		return nil
	end
	-- Check for dots
	if string.find(arg, '%.') then
		-- Bare IPv4
		local _,_,ip = string.find(arg, '^(%d+%.%d+%.%d+%.%d+)$') 
		if ip then
			-- TODO: Make sure this isn't one of *our* interface IP addresses
			add_rejectable(conn.rejectables, {
				name = 'strict_helo',
				sub_test = 'bare_ip',
				output = '550 Invalid HELO; argument cannot be a bare IP address'
			})
			return nil
		end
		-- IPv4 address literal
		local _,_,ip = string.find(arg, '^%[(%d+%.%d+%.%d+%.%d+)%]$')
		if ip then
			-- Check domain literal matches IP
			if ip == client.address then
				return nil
			else
				-- Mismatch
				add_rejectable(conn.rejectables, {
					name = 'strict_helo',
					sub_test = 'ip_mismatch',
					output = '550 Invalid HELO; address literal does not match client address'
				})
				return nil
			end
		end
		-- Check that we have valid characters now that address literals have been checked
		if string.find(arg, '^[a-zA-Z0-9%-%._]+$') then
			-- Check for double-dots
			if string.find(arg, '%.%.') then
				add_rejectable(conn.rejectables {
					name = 'strict_helo',
					sub_test = 'double_dots',
					output = '550 Invalid HELO; argument contains invalid host/domain'
				})
				return nil
			end
			-- See if the domain is a valid TLD
			local host, domain = split_host_domain(arg)
			if domain then 
				if host then
					if arg:lower() == client.host:lower() then
						conn.flags['helo_matches_rdns'] = true;
					end
					return nil;
				else
					conn.flags['helo_domain_only'] = true;
					return nil; 
				end
			end -- if domain
			-- NOTE: IF WE GET HERE; THEN THE TLD USED WAS NOT VALID
			-- Check TLD and make some exceptions
			local _,_,tld = string.find(arg:lower(), '%.([^%.]+)$')
			if tld then
				if (tld == 'localdomain' and net.is_ip_reserved(client.address, net.is_ip.LOCAL))
				or tld == 'lan' or tld == 'local' 
				then
					return nil
				else 
					add_rejectable(conn.rejectables, {
						name = 'strict_helo',
						sub_test = 'invalid_tld',
						output = string.format('550 Invalid HELO; domain contains invalid TLD (%s)', tld)
					})
					return nil
				end
			end -- if tld
		else 
			add_rejectable(conn.rejectables, {
				name = 'strict_helo',
				sub_test = 'invalid_chars',
				output = '550 Invalid HELO; argument contains invalid characters'
			})
			return nil
		end -- if string.find(arg, '^[a-zA-Z0-9%-%._]+$')
	else
		-- Allow bare names from local or LAN addresses
		if net.is_ip_reserved(client.address, net.is_ip.LOCAL + net.is_ip.LAN) then
			return nil
		end
		-- No dots; can't be valid
		add_rejectable(conn.rejectables, {
			name = 'strict_helo',
			sub_test = 'not_dots',
			output = '550 Invalid HELO; argument must be host/domain or address literal'
		})
		return nil
	end -- if string.find(arg, '%.')
	-- Catch all..
	add_rejectable(conn.rejectables, {
		name = 'strict_helo',
		sub_test = 'unknown',
		output = '550 Invalid HELO'
	})
end

function hook.helo(arg)
	debug('hook.helo: arg='..arg)
	conn.ehlo_no_helo = false;

	-- Check for pipelining
	check_for_pipelining(client.is_pipelining, false)
	-- Check command case
	check_command_case(client.input)

	-- TODO: Access Map lookup

	-- Check for schizophrenia
	if conn.helo and conn.helo ~= arg then
		add_rejectable(conn.rejectables, {
			name = 'helo_schizophrenic',
			sub_test = 'short_term',
			output = string.format('550 5.7.1 Host %s [%s] is schizophrenic (1)', client.host, client.address)
		})
	else
		-- Store
		conn.helo = arg
	end

	-- TODO: Check for HELO domain in URI/Domain blacklists

	-- Check for validity
	verify_helo(arg)

	-- IP in HELO?
	if net.is_ipv4_in_name(client.address, arg) > 0 then
		conn.flags['ip_in_helo'] = true
	end

	-- Long term schizophenic?
	local helo_key = 'helo:'..client.address
	local stored_helo = redis:get(helo_key)
	if stored_helo then
		if stored_helo ~= arg then
			add_rejectable(conn.rejectables, {
				name = 'helo_schizophrenic',
				sub_test = 'long_term',
				output = string.format('550 5.7.1 Host %s [%s] is schizophrenic (2)', client.host, client.address)
			})
			-- Update stored record
			redis:set(helo_key, arg)
			redis:expire(helo_key, conf.helo_ttl)
		end
	else
		-- Store HELO argument
		redis:set(helo_key, arg)
		redis:expire(helo_key, conf.helo_ttl)
	end
end

function hook.auth(arg)
	debug('hook.auth: arg='..arg)
	-- Check command case
	check_command_case(client.input)

	-- TODO: implement AUTH proxy
	-- TODO: how to report capabilities?

	return '550 AUTH rejected'
end

function hook.unknown(input)
	-- TODO: Check for pipelining

	-- Check command case
	check_command_case(client.input)

	-- OTHER IMPLEMENTED COMMANDS HERE
	local _,_,cmd = string.find(input:lower(), '^([^ ]+)')
	local _,_,args = string.find(input:lower(), '([^ ]+)$')
	if cmd == 'expn' then
		conn.expn_count = conn.expn_count + 1
		return '502 Command not implemented'
	elseif cmd == 'vrfy' then
		conn.vrfy_count = conn.vrfy_count + 1
		return '502 Command not implemented'
	elseif cmd == 'test' then
		return '250 Testing 1..2..3..4..5..'
	elseif cmd == 'stat' and args == 'dnslists' then
		local output = ''
		for _,key in pairs(redis:keys('dns-list-stat:*')) do
			for rcode, count in pairs(redis:hgetall(key)) do
				-- Strip key prefix
				local _,_,list = string.find(key, '^[^:]+:(.+)%.$')
				client.write('250-'..string.format('%-40s %-10s => %5s',list,rcode,count).."\r\n")
			end
		end
		return '250 end'
	end

	-- Increment unknown command counter
	conn.unknown_count = conn.unknown_count + 1;
	debug(string.format('hook.unknown: arg=%s count=%d', input, conn.unknown_count))
	-- TODO: log line
end

function hook.mail(sender, domain) 
	debug(string.format('hook.mail: sender="%s" domain="%s"', sender, domain))
	txn.id = client.id_trans

	local sender_flags = {}

	-- Increment transaction counter
	conn.txn_count = conn.txn_count + 1
	-- Check for pipelining
	check_for_pipelining(client.is_pipelining, conn.flags['esmtp'])
	-- Record data
	txn.sender = sender
	-- Check command case
	check_command_case(client.input)

	-- Check for extra spaces
	if string.find(client.input, ': <') then
		conn.flags['cmd_mail_extra_spaces'] = true
		txn.flags['cmd_mail_exta_spaces'] = true
		sender_flags['extra_spaces'] = true
		debug('hook.mail: found extra spaces')
	end

	-- Check for extra parameters
	_,_,params = string.find(client.input, '> (.+)$')
	if params then
		debug('hook.mail: extra parameters found: '..params)
		-- Check that we are in SMTP mode...
		if not conn.flags['esmtp'] then
			sender_flags['extra_params'] = true
			add_rejectable(txn.rejectables, {
				name = 'invalid_params',
				sub_name = 'mail',
				output = '555 Unsupported MAIL parameters sent'
			})
		else
			-- Split parameters into words
			for param in params:gmatch('([^ ]+)') do
				local match = false
				-- Check parameter pairs
				for k, v in param:gmatch('([^= ]+)=([^= ]+)') do
					if string.upper(k) == 'SIZE' then
						txn.size = tonumber(v) or 0
						match = true
					elseif string.upper(k) == 'BODY' then
					else
						-- return '555 Unsupported MAIL parameter \''..k..'\''
					end
				end
				-- If match is false; then we have an invalid parameter
				-- that is not in key=value format and reject accordingly.
				if not match then
					-- Invalid parameter
					add_rejectable(txn.rejectables, {
						name = 'invalid_params',
						sub_test = 'mail:'..param,
						output = string.format('555 Unsupported MAIL parameter \'%s\'', param)
					})
				end
			end
		end
	end

	-- Access Map
	local access = accessMail(client.address, client.host, sender)
	if access and access.action then
		if access.action == 'OK' then 
			txn.flags['white'] = true
		elseif access.action == 'CONTENT' then
			txn.flags['content'] = true
		elseif access.action == 'REJECT' then
			add_rejectable(txn.rejectables, {
				name = 'black',
				output = ternary(access.action_text, access.action_text, string.format('550 sender <%s> black listed', sender))
			})
		elseif access.action == 'IREJECT' then
			return true, ternary(access.action_text, access.action_text, string.format('550 sender <%s> black listed', sender))
		elseif access.action == 'DISCARD' then
			txn.flags['discard'] = true
		elseif access.action == 'TAG' then
			txn.flags['tag'] = true
		elseif access.action == 'SAVE' then
			txn.flags['save'] = true
			txn.quarantine = true
			if access.action_text then
				txn.quarantine_type = access.action_text
			else
				txn.quarantine_type = 'save'
			end
		elseif access.action == 'TRAP' then
			txn.flags['trap'] = true
			txn.quarantine = true
			if access.action_text then
				txn.quarantine_type = access.action_text
			else
				txn.quarantine_type = 'trap'
			end
		else
			syslog.error(string.format('access-map unknown action \'%s\'', access.action))
		end
	end

	-- Lookup domain in route-map if necessary
	if not domains[domain] then
		domains[domain] = routeGetHosts(sender)
	end

	-- Strip domain
	if domain and domain ~= '' and not domains[domain] then    -- Don't run these check for our own domains!
		-- Store
		if not conn.domain then
			conn.domain = domain
		end
		-- Make sure we get an MX 
		-- Don't allow messages that cannot be replied to
		dns.open()
		dns.query(dns.class.IN, dns.type.MX, domain..'.')
		local mx_result = dns.wait(1)
		dns.close()
		local mx_answer = mx_result['IN,MX,'..domain..'.']
		if mx_answer then
			if mx_answer.rcode == dns.rcode.OK then
				local mx_count = 0
				for host, ip in pairs(get_mx_list(domain)) do
					-- Client is MX?
					if client.address == ip then
						txn.flags['host_is_mx'] = true;
						sender_flags['host_is_mx'] = true;
					end
					mx_count = mx_count + 1
				end
				-- Did we find any valid MX records that correctly resolved?
				if mx_count == 0 then
					add_rejectable(txn.rejectables, {
						name = 'mail_mx_required',
						output = string.format('550 Domain \'%s\' does not have any valid MX records', domain)
					}) 
				end	
			elseif mx_answer.rcode == dns.rcode.NXDOMAIN then
				add_rejectable(txn.rejectables, {
					name = 'mail_mx_required',
					output = string.format('550 Domain \'%s\' does not resolve', domain)
				})
			else
				-- Some other DNS error = TEMPFAIL
				add_rejectable(txn.rejectables, {
					name = 'mail_mx_required',
					output = string.format('451 Error resolving MX records for domain \'%s\' (%s)', domain, dns.rcodename(mx_result.rcode))
				})	
			end
		else
			-- Unknown DNS result = TEMPFAIL
			add_rejectable(txn.rejectables, {
				name = 'mail_mx_required',
				output = string.format('451 Error resolving MX records for domain \'%s\'', domain)
			})
		end
		-- TODO: SPF
	end

	-- Quick hack check for SPF record existance
	dns.open()
	dns.query(dns.class.IN, dns.type.TXT, domain..'.')
	local spf_result = dns.wait(1)
	dns.close()
	local spf_answer = spf_result['IN,TXT,'..domain..'.']
	if spf_answer and spf_answer.answer then
		local answers = spf_answer.answer
		for answer, rr in pairs(answers) do
			local _,_,spf_record = string.find(rr.value, '^(v=spf.+)')
			if spf_record then
				debug('found spf record: '..spf_record)
				-- Store SPF record for later testing
				txn.spf_record = spf_record
				txn.flags['mail_has_spf'] = true
				sender_flags['mail_has_spf'] = true
			end
		end
	end

	-- One domain per connection
	if (domain and (conn.domain and conn.domain ~= domain)) and 
	not (net.is_ip_reserved(client.address, net.is_ip.LOCAL) or conn.flags['relay'])
	then
		add_rejectable(conn.rejectables, {
			name = 'one_domain_per_conn',
			output = '450 Only one sender domain per connection is allowed from your host'
		})
	end

	-- TODO: strict 'freemail' e.g. from domain must appear in rDNS

	-- Log point
	syslog.info(
		string.format(
			'sender <%s> f="%s"',
			sender, table_concat_keys(sender_flags) 
		)
	)
end

function hook.rcpt(rcpt, domain)
	debug(string.format('hook.rcpt: rcpt="%s" domain="%s"', rcpt, domain))
	local rcpt_rejectables = {}

	-- Check for pipelining
	check_for_pipelining(client.is_pipelining, conn.flags['esmtp'])
	-- Check command case
	check_command_case(client.input)

	-- Check for extra spaces
	if string.find(client.input, ': <') then
		conn.flags['cmd_rcpt_extra_spaces'] = true
		txn.flags['cmd_rcpt_extra_spaces'] = true
		debug('hook.rcpt: found extra spaces')
	end

	-- Check for extra parameters
	_,_,params = string.find(client.input, '> (.+)$')
	
	if params then
		debug('hook.rcpt: extra parameters found: '..params)
		-- Check that we are in SMTP mode...
		if not conn.flags['esmtp'] then
			add_rejectable(txn.rejectables, {
				name = 'invalid_params',
				sub_test = 'rcpt',
				output = '555 Unsupported RCPT parameters sent'
			})
		else
			-- Split parameters into words
			for param in params:gmatch('([^ ]+)') do
				local match = false
				-- Check parameter pairs
				for k, v in param:gmatch('([^= ]+)=([^= ]+)') do
					if string.upper(k) == 'XFOO' then
						-- Implement parameters here...
						match=true
					end
				end
				-- If match is false; then we have an invalid parameter
				-- that is not in key=value format and reject accordingly.
				if not match then
					-- Invalid parameter
					add_rejectable(txn.rejectables, {
						name = 'invalid_params',
						sub_test = 'rcpt:'..param,
						output = string.format('555 Unsupported RCPT parameter \'%s\'', param)
					})
				end
			end
		end
	end

	-- Access Map
	local access = accessRcpt(client.address, client.host, txn.sender, rcpt)
	if access and access.action then
		if access.action == 'OK' or access.action == 'CONTENT' then
			return rcpt_action(rcpt, string.format('250 2.1.0 recipient <%s> OK', rcpt), 'rcpt_white')
		elseif access.action == 'REJECT' or access.action == 'IREJECT' then
			return rcpt_action(rcpt, string.format('550 recipient <%s> rejected; black listed', rcpt), 'rcpt_black')
		elseif access.action == 'DISCARD' then
			txn.flags['discard'] = true
		elseif access.action == 'TAG' then
			txn.flags['tag'] = true
		elseif access.action == 'SAVE' then
			txn.flags['save'] = true
			txn.quarantine = true
			if access.action_text then
				txn.quarantine_type = access.action_text
			else
				txn.quarantine_type = 'save'
			end
		elseif access.action == 'TRAP' then
			txn.flags['trap'] = true
			txn.quarantine = true
			if access.action_text then
				txn.quarantine_type = access.action_text
			else
				txn.quarantine_type = 'trap'
			end
		else
			syslog.error(string.format('access-map unknown action \'%s\'', access.action))
		end
	end

	-- One recipient per null
	if (txn.sender and txn.sender == '') and #txn.rcpts_accepted > 0 then
		add_rejectable(txn.rejectables, {
			name = 'one_rcpt_per_null',
			output = '550 Only one recipient is allowed for messages with a null reverse path'
		})
	end

	-- Lookup domain in route-map if necessary
	-- TODO: turn this into a function...
	if not domains[domain] then
		domains[domain] = routeGetHosts(rcpt)
	end

	-- Is the recipient in one of our domains?
	if domains[domain] then
		-- Inbound message

		-- Verify recipient
		local verify = verify_rcpt(rcpt, domains[domain][1])
		if verify == 5 then
			return rcpt_action(rcpt, string.format('550 recipient <%s> unknown', rcpt), 'rcpt_unknown')
		elseif not verify or verify == 4 then
			return rcpt_action(rcpt, string.format('450 recipient <%s> verification error', rcpt), 'rcpt_verify_error')
		end

		-- Check message size; might have been passed
		-- as SIZE= parameter to MAIL command.
		if txn.size > 0 and (conf.message_max_size and tonumber(txn.size) > conf.message_max_size) then
			-- TODO: preference limits
			return rcpt_action(rcpt, string.format('550 recipient <%s> message size %d bytes exceeds limit (%d bytes)', rcpt, txn.size, conf.message_max_size), 'max_message_size')
		end

		-- Address book pre-DATA whitelist?	
		if redis:sismember('abook:'..rcpt:lower(), string.lower(txn.sender)) then
			txn.flags['abook_wl'] = true
		end
	else
		-- Outbound message

		-- Is this host allowed to relay?
		if not conn.flags['relay'] or not net.is_ip_reserved(client.address, net.is_ip.LOCAL) then
			return rcpt_action(rcpt, string.format('550 <%s> relaying denied', rcpt), 'relay')
		end

		-- Strict relay rules?
		local _,_,sdomain = string.find(string.lower(txn.sender),'@(.+)$')
		-- We only allow outbound domains that we allow inbound
		if not domains[sdomain] then
			domains[sdomain] = routeGetHosts(sdomain)
		end
		if conf.rejectables['strict_relay'] and (sdomain and not domains[sdomain]) then
			return rcpt_action(rcpt, string.format('550 <%s> relaying denied from domain \'%s\'', rcpt, sdomain), 'strict_relay')
		end

		txn.flags['outbound'] = true
	end

	-- Bypass any rejections as necessary
	if (conn.flags['white'] or txn.flags['white'])
	or (conn.flags['content'] or txn.flags['content'])
	or (conn.flags['dns_wl'] or conn.flags['dns_yl'])
	or txn.flags['abook_wl']
	or (conn.flags['discard'] or txn.flags['discard'])  -- DISCARD
	or (conn.flags['tag'] or txn.flags['tag']) -- TAG
	or (conn.flags['trap'] or txn.flags['trap']) -- TRAP
	then
		return rcpt_action(rcpt, string.format('250 2.1.0 recipient <%s> OK', rcpt))
	end

	-- Apply any rejectables
	if conn.rejectables and #conn.rejectables > 0 then
		for _,reject in ipairs(conn.rejectables) do
			if conf.rejectables[reject.name] ~= false then
				return rcpt_action(rcpt, reject.output, reject.name)
			else
				debug(string.format('skipping disabled rejectable: %s', reject.name))
			end
		end
	end
	if txn.rejectables and #txn.rejectables > 0 then
		for _,reject in ipairs(txn.rejectables) do
			if conf.rejectables[reject.name] ~= false then
				return rcpt_action(rcpt, reject.output, reject.name)
			else
				debug(string.format('skipping disabled rejectable: %s', reject.name))
			end
		end
	end

	-- If not outbound; apply greylisting here if it is enabled
	-- Greylisting
	local grey_key = string.format('grey:%s,%s,%s', conn.host_id, txn.sender, rcpt:lower())
	debug(string.format('greylisting: key=%s', grey_key))
	if conf.rejectables['greylisting'] ~= false and (not (txn.flags['outbound'] or conn.flags['passed_greylist'])) then
		-- Check for previous entry
		local grey_result = redis:get(grey_key)
		local time_now = os.time(os.date('!*t'))	-- NOTE: UTC
		if grey_result then
			if time_now > (grey_result + conf.greylist_delay) then
				-- Total delay
				local delay = (time_now - grey_result)
				-- Create pass record
				redis:set('grey-pass:'..conn.host_id, delay)
				redis:expire('grey-pass:'..conn.host_id, conf.greylist_pass_ttl)
				-- Delete old record
				redis:del(grey_key)
				-- Skip further greylisting for this connection
				conn.flags['passed_greylist'] = true
			else
				local time_left = (grey_result + conf.greylist_delay) - time_now
				-- Extend expiry
				redis:expire(grey_key, conf.greylist_key_ttl) -- 25 hours
				return rcpt_action(rcpt, string.format('451 recipient <%s> greylisted for %d seconds', rcpt, time_left), 'greylisting')
			end
		else
			-- Create greylist record
			redis:set(grey_key, time_now)
			redis:expire(grey_key, conf.greylist_delay)
			return rcpt_action(rcpt, string.format('451 recipient <%s> greylisted for %d seconds', rcpt, conf.greylist_delay), 'greylisting')
		end
	else
		-- Tidy-up any remaining greylist keys
		redis:del(grey_key)
		-- Update key expiry time on pass record
		redis:expire('grey-pass:'..conn.host_id, conf.greylist_pass_ttl)
	end

	-- If we get to here, then accept the recipient
	return rcpt_action(rcpt, string.format('250 2.1.0 recipient <%s> OK', rcpt))
end

function rcpt_action(rcpt,msg,name)
	local code = tonumber(string.sub(msg,1,1))
	if code == 2 then
		-- ACCEPT
		table.insert(txn.rcpts_accepted, rcpt)
		conn.rcpts_accepted = conn.rcpts_accepted + 1
		redis:hincrby('stats:'..txn.date, 'rcpt_accept', 1)
		if name then
			redis:hincrby('stats:'..txn.date, 'rcpt_accept:'..name, 1)
		end
	elseif code == 4 then
		-- TEMPFAIL
		table.insert(txn.rcpts_tempfail, rcpt)
		conn.rcpts_tempfail = conn.rcpts_tempfail + 1
		redis:hincrby('stats:'..txn.date, 'rcpt_tempfail', 1)
		if name then
			redis:hincrby('stats:'..txn.date, 'rcpt_tempfail:'..name, 1)
		end
	elseif code == 5 then
		-- REJECT
		table.insert(txn.rcpts_rejected, rcpt)
		conn.rcpts_rejected = conn.rcpts_rejected + 1
		redis:hincrby('stats:'..txn.date, 'rcpt_reject', 1)
		if name then
			redis:hincrby('stats:'..txn.date, 'rcpt_reject:'..name, 1)
		end
	else
		-- ERROR
		syslog.error(string.format('rcpt_action: unhandled code (%d)', code))
		redis:hincrby('stats:'..txn.date, 'rcpt_error', 1)
		return '450 Internal error'
	end
	-- Log point
	syslog.info(string.format(
		'recipient <%s> cr="%s" tr="%s" x="%s"',
		rcpt,
		table_concat_keys(conn.rejectables),
		table_concat_keys(txn.rejectables),
		msg
		)
	)	
	return msg
end

function hook.data()
	debug('hook.data')

	-- Check for pipelining
	check_for_pipelining(client.is_pipelining, conn.flags['esmtp'])
	-- Check command case
	check_command_case(client.input)
	-- Check for extra parameters
	_,_,params = string.find(client.input, "^[Dd][Aa][Tt][Aa] (.+)$")
	if params then
		debug('hook.data found extra parameters: '..params)
	end

	-----------------
	-- Preferences --
	-----------------
	-- TODO: Complete 
	for i, rcpt in ipairs(txn.rcpts_accepted) do
		local _,_,domain = rcpt:find('@(.+)$')
		if domain and not txn.rcpt_domains[domain] then
			txn.rcpt_domains[domain] = 1
			table.insert(txn.rcpt_domains, domain)
		end
	end
	
	-- Work out what level of post-DATA preferences can be used
	if #txn.rcpts_accepted == 1 then
		txn.prefs = txn.rcpts_accepted[1];
	elseif #txn.rcpt_domains == 1 then
		-- Domain
		txn.prefs = txn.rcpt_domains[1]
	else
		-- Global
		txn.prefs = 'GLOBAL'
	end

	-- Log Point
	syslog.info(string.format(
		'DATA rcpts=(a=%d,t=%d,r=%s) prefs="%s"',
		#txn.rcpts_accepted or 0,
		#txn.rcpts_tempfail or 0,
		#txn.rcpts_rejected or 0,
		txn.prefs
		)
	)

end

function hook.header(line)
	_,_,name,value = string.find(line, "^([^ ]+):%s+(.+)")
	if name and value then
		-- debug(string.format('found header: header="%s" value="%s"', name, value))
		-- Lower-case the header name
		name = name:lower()
		if not txn.headers[name] then
			-- Initalize table for this header
			txn.headers[name] = {}
		end
		table.insert(txn.headers[name], value)
	end
end

function hook.content(chunk)
	-- Add to message size
	txn.size = txn.size + chunk:len()
	conn.bytes_in = conn.bytes_in + chunk:len()
end

function hook.eoh()
	debug('hook.eoh')

	-- Check RFC 5322 required headers (From, Date)
	if txn.headers['date'] and txn.headers['date'][1]  then
		-- TODO: parse date header
	else
		add_rejectable(txn.dot_rejectables, {
			name = 'rfc5322_min_headers',
			sub_test = 'date_required',
			output = '550 Message rejected; no \'Date\' header found'
		})
	end
	if not txn.headers['from'] or (txn.headers['from'] and not txn.headers['from'][1]) then
		add_rejectable(txn.dot_rejectables, {
			name = 'rfc5322_min_headers',
			sub_test = 'from_required',
			output = '550 Message rejected; no \'From\' header found'
		})
	end
	-- Make sure that multiple headers are not present (RFC 5322 Section 3.6)
	if (txn.headers['date'] and #txn.headers['date'] > 1) or
	   (txn.headers['from'] and #txn.headers['from'] > 1) or
	   (txn.headers['sender'] and #txn.headers['sender'] > 1) or
	   (txn.headers['reply-to'] and #txn.headers['reply-to'] > 1) or
	   (txn.headers['to'] and #txn.headers['to'] > 1) or
	   (txn.headers['cc'] and #txn.headers['cc'] > 1) or
	   (txn.headers['bcc'] and #txn.headers['bcc'] > 1) or
	   (txn.headers['message-id'] and #txn.headers['message-id'] > 1) or
	   (txn.headers['in-reply-to'] and #txn.headers['in-reply-to'] > 1) or
	   (txn.headers['references'] and #txn.headers['references'] > 1) or
	   (txn.headers['subject'] and #txn.headers['subject'] > 1) then
		add_rejectable(txn.dot_rejectables, {
			name = 'rfc5322_max_headers',
			output = '550 Message rejected; contains non-unique message headers that should be unique'
		})
	end

	-- Already marked as spam?
	if (txn.headers['x-spam-flag'] and string.match(txn.headers['x-spam-flag'][1], '^[Yy][Ee][Ss]$'))
	or (txn.headers['x-spam-status']  and string.match(txn.headers['x-spam-status'][1], '^[Yy][Ee][Ss]'))
	then
		add_rejectable(txn.dot_rejectables, {
			name = 'already_spam',
			output = '550 Message rejected; already marked as spam by an upstream host'
		})
	end

	local _,_,sdomain = string.find(string.lower(txn.sender), '@(.+)$')
	if sdomain and not domains[sdomain] then
		domains[sdomain] = routeGetHosts(sdomain)
	end
	if txn.flags['outbound'] and (sdomain and domains[sdomain]) then	-- Only for our inbound domains
		-- TODO: Electronic watermarks

		-- Addressbook auto-whitelist
		-- Only add to addressbook if message was not auto-generated
		if not (txn.headers['auto-submitted'] and string.lower(txn.headers['auto-submitted'][1]) ~= 'no') and
		   not (txn.headers['precedence'] and 
		     (string.format(txn.headers['precedence'][1]) ~= 'bulk' or
		      string.format(txn.headers['precedence'][1]) ~= 'list' or
		      string.format(txn.headers['precedence'][1]) ~= 'junk')
		   )
                then
			for _, rcpt in ipairs(rcpts) do
				redis:sadd('abook:'..string.lower(txn.sender), rcpt:lower())
				debug(string.format('abook: adding %s to %s address book', txn.sender, rcpt))
			end
			-- Touch expire time on addressbook
			redis:expire('abook:'..txn.sender, conf.abook_ttl)
		end
	end

	-- EMEW: check for watermarks on inbound mail

	-- Add our Received header to the top of the list
	header.insert( 
		string.format(
			'Received: from %s (%s [%s])\r\n' ..
			'\tby %s ([%s]) envelope-from <%s> with %s\r\n' ..
			'\tid %s ret-id %s; %s',
	  	conn.helo, client.host, client.address, 
		smtpe.host, client.local_address, txn.sender, ternary(conn.flags['esmtp'], 'ESMTP', 'SMTP'), 
		client.id_trans, 'none', os.date('%a, %d %b %Y %H:%M:%S %z'))
	, 1)
end

function hook.body(line)
	-- Not used at this time
end

function hook.dot()
	debug('hook.dot')

	-- Check message size
	if txn.size > 0 and (conf.message_max_size and tonumber(txn.size) > conf.message_max_size) then
		-- TODO: preference limits
		add_rejectable(txn.dot_rejectables, {
			name = 'max_message_size',
			output = string.format('550 message rejected; message size %d bytes exceeds limit (%d bytes)', txn.size, conf.message_max_size)
		})
	end

	-- MIME
	txn.mime_types = {}
	txn.filenames = {}
	txn.fileextns = {}
	txn.charsets = {}
	if #mime.parts > 0 then
		for p, part in ipairs(mime.parts) do
			if part.content_type then
				-- MIME type
				local _,_,mtype = string.find(part.content_type:lower(),'^content%-type:%s*([^; ]+)')
				if mtype then
					debug('Found MIME type: '..mtype)
					table_insert_nodup(txn.mime_types, mtype)
				end
				-- Filename
				local _,_,file = string.find(part.content_type,'[Nn][Aa][Mm][Ee]="?([^;"]+)"?')
				if file then
					table.insert(txn.filenames, file)
					file = file:lower()
					debug('Found attachment: '..file)
					local _,_,extn = file:find('%.([^%.]+)$')
					if extn then
						table_insert_nodup(txn.fileextns, extn)
						debug('Found attached file extension: '..extn)
					end
					-- TODO: dispositions
				end
				-- Charset
				local _,_,charset = string.find(part.content_type:lower(),'charset="?([^;"]+)"?')
				if charset then
					table_insert_nodup(txn.charsets, charset)
					debug('Found charset: '..charset)
				end
			end
		end
	end
	-- Charsets
	-- Get other potential charsets from Content-Type and Subject headers
	if txn.headers['content-type'] then
		local _,_,charset = string.find(string.lower(txn.headers['content-type'][1]), 'charset="?([^;"]+)"?')
		if charset then
			table_insert_nodup(txn.charsets, charset)
			debug(string.format('Found charset: \'%s\' in Content-Type header', charset))
		end
	end
	if txn.headers['subject'] then
		local _,_,charset = string.find(string.lower(txn.headers['subject'][1]), '^=%?([^%s]+)%?[BQ]%?')
		if charset then
			table_insert_nodup(txn.charsets, charset)
			debug(string.format('Found charset \'%s\' in Subject header', charset))
		end
	end

	-- TODO: Filename/Filetype checks

	lua_content_filters()

	-- TODO: Custom Bayes?

--[[
	-- TODO: OSBF-Lua
	local osbf_start = socket.gettime()
	if not file_exists(conf.osbf_dbset.classes[1]) or not file_exists(conf.osbf_dbset.classes[2]) then
		-- Create databases
		syslog.info('OSBF: creating new databases')
		osbf.remove_db(conf.osbf_dbset.classes)
		local r, err = osbf.create_db(conf.osbf_dbset.classes, 94321)
		if not r then
			syslog.error('OSBF: error creating databases ('..err..')')
		end
	end
	local h = io.open(client.msg_file, 'r')
	if h then
		local msg = h:read('*all')
		h:close()
		-- Trim to .5Mb max
		msg = msg:sub(1, 500000)
		syslog.info('OSBF: message size is '..msg:len())
		-- Run classify
		local pR, p_array, i_pmax = osbf.classify(msg, conf.osbf_dbset, 0)
		if pR == nil then
			syslog.error('OSBF: classify error ('..p_array..')')
		else
			local osbf_elapsed = socket.gettime() - osbf_start
			syslog.info(string.format('OSBF: elapsed=%.2f pR=%f', osbf_elapsed, pR))
		end
		msg = nil
	else
		syslog.error('OSBF: error opening transport file')
	end
]]--

	-- URI checks
	-- TODO: limit number of URIs that can be checked
	local uri_hosts_to_check = {}
	local uri_domains_to_check = {}
	local uri_shortened_to_check = {}
	local uri_mailto_to_check = {}
	if uri.found and #uri.found > 0 then
		for n, md5 in ipairs(uri.found) do
			local current_uri = uri.found[md5]
			if current_uri and current_uri.host then
				local uri_host = string.lower(current_uri.host)
				local uri_domain = hostname_to_domain(uri_host)
				local uri_raw = string.lower(current_uri.uri_raw)
				table_insert_nodup(uri_hosts_to_check, uri_host)
				if uri_domain then
					table_insert_nodup(uri_domains_to_check, uri_domain)
				end
				if current_uri.scheme == 'mailto' then
					table_insert_nodup(uri_mailto_to_check, uri_raw)
				end
				if uri_domain and (url_shorteners[uri_domain] and (current_uri.path and current_uri.path ~= '/')) then
					table_insert_nodup(uri_shortened_to_check, current_uri.uri_raw)
				end
				-- TODO: resolve names to IP addresses and look-up in SBL
				-- TODO: resolve names to authoratative namservers
			end
		end
	end

	-- Log point
	syslog.info('URIs found:'..
		' TOTAL=' .. #uri.found ..
		', hosts=' .. #uri_hosts_to_check ..
		', domains=' .. #uri_domains_to_check ..
		', shortened=' .. #uri_shortened_to_check ..
		', mailto=' .. #uri_mailto_to_check
	)

	-- Limit the number of lookups
	trim_table_to_limit(uri_hosts_to_check, conf.uri_max_limit)
	trim_table_to_limit(uri_domains_to_check, conf.uri_max_limit)
	trim_table_to_limit(uri_shortened_to_check, conf.uri_max_limit)
	trim_table_to_limit(uri_mailto_to_check, conf.uri_max_limit)

	-- URIBL queries
	dns_list_lookup_uri(uri_domains_to_check, conf.uribl_domains)
	dns_list_lookup_uri(uri_hosts_to_check, conf.uribl_hosts)
	-- Unset
	uri_hosts_to_check = {}
	uri_domains_to_check = {}

	-- Resolve shortened URLs
	if uri_shortened_to_check and #uri_shortened_to_check > 0 then
		for _,short_uri in ipairs(uri_shortened_to_check) do
			service.http.request(short_uri, 'HEAD')
		end
		local sres = service.wait(1)
		if sres.http and #sres.http > 0 then
			for i, http in ipairs(sres.http) do
				if (http.rcode == 301 or http.code == 302) and http.headers then
					-- Try and parse location header
					local _,_,location = string.find(http.headers, '[Ll]ocation:%s+([^\r\n ]+)')
					if location then
						syslog.info(string.format('Found shortened URL: %s -> %s', http.url, location))
						-- TODO: check for ToS violation pages
						local parsed_uri = uri.parse(location)
						local uri_host = string.lower(parsed_uri.host)
						local uri_domain = string.lower(hostname_to_domain(uri_host))
						-- Make sure this isn't another shortener
						if not url_shorteners[uri_domain] then
							table_insert_nodup(uri_hosts_to_check, uri_host)
							table_insert_nodup(uri_domains_to_check, uri_domain)
						else
							-- Make sure it's not a redirection to the *same* shortener.
							local check = uri.parse(http.url)
							if not uri_domain == string.lower(check.host) then
								-- TODO: found shortener chain
								syslog.info(string.format('Found shortener chain: %s -> %s', check.uri_raw, location))
								-- What to do here... recurse to a certain level?
								-- Reject the message? (apparently some geniune cases...)
							end
						end
					end
				elseif http.rcode == 404 then
					-- TODO: resolves to a 404 page.
				else
					syslog.error(string.format('URI shortener returned unhandled rcode (%s) for URI %s', http.rcode, http.url))
				end
			end
			-- Redo DNS queries
			dns_list_lookup_uri(uri_domains_to_check, conf.uribl_domains)
			dns_list_lookup_uri(uri_hosts_to_check, conf.uribl_hosts)
		end
	end

	-- spamd/clamd
	if not conf.spamd_command then
		conf.spamd_command = 'SYMBOLS'
	end
	if not (service.spamd(client.msg_file, conf.spamd_hosts, conf.spamd_command, nil, 30)) then
		syslog.error('unable to connect to spamd!')
	end
	-- TODO: Optionally only send messages with binary attachments for extra efficiency
	if not (service.clamd(client.msg_file, conf.clamd_hosts, 30)) then
		syslog.error('unable to connect to clamd!')
	end
	local sres = service.wait(1)

	-- clamd
	-- TODO: Only send messages with attachments for extra efficiency
	if sres['clamd'] then
		local found,_,result = sres.clamd.reply:find('^[^%s:]+:%s+([^\r\n]+)')
		if found then
			if result ~= 'OK' then
				-- Result contains the virus name
				local _,_,virus = result:find('^([^%s]+) FOUND')
				add_rejectable(txn.dot_rejectables, {
					name = 'virus',
					sub_test = 'clamd',
					output = string.format('550 Message rejected; infected with virus \'%s\'', virus)
				})
					
			end
			local clamd_report = string.format('elapsed=%.2f host="%s" result="%s"', sres.clamd.elapsed_time, sres.clamd.service_host, result)
			syslog.info('clamd: '..clamd_report)
			header.modify('X-BarricadeMX-Clamd-Report', 1, clamd_report)
		else
			syslog.error('error parsing clamd result')
		end	
	end

	-- spamd
	-- TODO: limit message size sent to spamd
	-- TODO: optionally only send text/* and message/* parts to spamd
	if sres['spamd'] then
		-- Parse reply
		local found,_,is_spam,score,threshold = sres.spamd.reply:find("Spam:%s+([^%s]+)%s+;%s+([%d%.]+)%s+/%s+([%d%.]+)\r?\n")
		local _,_,body = sres.spamd.reply:find("\r?\n\r?\n(.+)\r?\n$")
		if found and body then
			txn.spamd_score = score
			-- Parse rule list from body if present (SYMBOLS)
			if conf.spamd_command == 'SYMBOLS' then
				txn.spamd_rules_hit = {}
				for rule in body:gmatch('([^,\r\n]+)') do
					txn.spamd_rules_hit[rule] = 1
				end
			end

			if is_spam:lower() == 'true' then
				-- See if score exceeds the rejection threshold
				if conf.spamd_score_reject and tonumber(score) >= conf.spamd_score_reject then
					add_rejectable(txn.dot_rejectables, {
						name = 'spamd',
						output = string.format('550 Message rejected; spam score (%.1f) exceeds threshold', score)
					})
				else
					txn.msg_needs_tagging = true
				end
			end
			local spamd_header = string.format('elapsed=%.2f host="%s" score=%.1f tests="%s"', sres.spamd.elapsed_time, sres.spamd.service_host, score, table_concat_keys(txn.spamd_rules_hit))
			syslog.info('spamd: '..spamd_header)
			header.modify('X-BarricadeMX-Spamd-Report', 1, spamd_header)
		else
			syslog.error(string.format('error parsing spamd output: %s', sres.spamd.reply))
		end
	end

	-- Finally add a status header
	local report_header = string.format(
		'id=%s cr="%s" tr="%s" dr="%s" rcpts=(a=%d,t=%d,r=%d) msgs=(a=%d,t=%d,r=%d)',
		txn.id,
		table_concat_keys(conn.rejectables),
		table_concat_keys(txn.rejectables),
		table_concat_keys(txn.dot_rejectables),
		conn.rcpts_accepted,
		conn.rcpts_tempfail,
		conn.rcpts_rejected,
		conn.msgs_accepted,
		conn.msgs_tempfail,
		conn.msgs_rejected
	)
	header.modify('X-BarricadeMX-Report', 1, report_header)

	-- Handle message tagging
	if txn.msg_needs_tagging or conn.flags['tag'] or txn.flags['tag'] then
		debug('tagging message')
		-- TODO: expand header additions
		header.modify('X-Spam-Flag', 1, 'YES')
		-- Modify subject
		local sub_idx, subject = header.find('subject', 1)
		if subject then
			header.modify('Subject', 1, '[SPAM] '..subject)
		end
		-- Set 'Precedence' header to junk (or add if not already present)
		header.modify('Precedence', 1, 'junk')
	end

	-- hook.forward returns response
end

function hook.error(errno, error_text)
	-- Connection timed out (110)
	if errno == 110 then
		conn.timed_out = true
		conn.conn_error = true
	end
	debug(string.format('hook.error: errno=%d error_text="%s"', errno, error_text))
end

function hook.close(was_dropped)
	debug('hook.close: was_dropped='..was_dropped)
	if was_dropped and was_dropped == 1 then
		conn.flags['dropped'] = true
		debug('hook.close: connection was dropped')
	end
	if conn.conn_error then
		debug('hook.close: connection error')
	elseif conn.timed_out then
		debug('hook.close: connection timed out')
	elseif not conn.clean_quit then
		debug('hook.close: host did not send QUIT')
	elseif conn.clean_quit then
		debug('hook.close: clean exit')
	else
		debug('hook.close: unknown reason')
	end
	local end_time = socket.gettime()
	conn.total_time = end_time - conn.start_time

	-- Log point
	syslog.info(
		string.format(
			'end i=%s p="%s" h="%s" f="%s" r="%s" rcpts=(a=%d,t=%d,r=%d) msgs=(a=%d,t=%d,r=%d) le="%s" bytes(in/out)=%d/%d ct=%.2f', 
			client.address, 
			client.host,
			conn.helo or "",
			table_concat_keys(conn.flags),
			table_concat_keys(conn.rejectables),
			conn.rcpts_accepted, 
			conn.rcpts_tempfail, 
			conn.rcpts_rejected, 
			conn.msgs_accepted, 
			conn.msgs_tempfail, 
			conn.msgs_rejected,
			conn.last_error or '',
			conn.bytes_in,
			conn.bytes_out,
			conn.total_time
		)
	)	
end

function hook.forward(path, sender, rcpts)
	debug('hook.forward: path='..path)

	-- Handle TRAP
	if conn.flags['trap'] or txn.flags['trap'] then
		return msg_action('550 message rejected; unspecified reasons', 'trap')
	end

	-- Handle DISCARD
	if conn.flags['discard'] or txn.flags['discard'] then
		return msg_action(string.format('250 message <%s> accepted', txn.id), 'discard')
	end 

	-- Handle infected messages separately to prevent whitelisting
	if txn.dot_rejectables['virus'] then
		reject = txn.dot_rejectables[txn.dot_rejectables['virus']]
		return msg_action(reject.output, reject.name)
	end

	-- Check rejectables 
	if not (conn.flags['white'] or txn.flags['white'] or conn.flags['dns_wl'])
	and txn.dot_rejectables and #txn.dot_rejectables > 0 then
		for _,reject in ipairs(txn.dot_rejectables) do
			if conf.rejectables[reject.name] ~= false then
				-- Handle TAG
				if not (conn.flags['tag'] or txn.flags['tag']) then
					return msg_action(reject.output, reject.name)
				end
			else
				debug(string.format('skipping disabled rejectable: %s', reject.name))
			end
		end
--[[
-- Train OSBF

        local h = io.open(client.msg_file, 'r')
        if h then
                local msg = h:read('*all')
                h:close()
                -- Trim to .5Mb max
                msg = msg:sub(1, 500000)
                -- Train as spam
		local r, err = osbf.learn(msg, conf.osbf_dbset, 2, 0)
		if not r then
			syslog.error('OSBF: training error ('..err..')')
		end
                msg = nil
        else
                syslog.error('OSBF: error opening transport file')
        end
]]--
	end

	-- Record message details
	local log = {
		["date"] = os.time(os.date('!*t')),
		["sess_id"] = conn.id,
		["txn_id" ] = txn.id,
		["clientip"] = client.address,
		["clientptr"] = client.host,
		["from"] = txn.sender,
		["rcpts"] = txn.rcpts_accepted,
		["size"] = nil,
		["subject"] = nil,
		["spamd_score"] = txn.spamd_score or 0,
		["spamd_rules_hit"] = table_concat_keys(txn.spamd_rules_hit) or nil,
		-- Connection flags
		-- Transaction flags
	}
	-- Un-fold and add in subject header
	if txn.headers['subject'] and #txn.headers['subject'] > 0 then
		log["subject"] = string.gsub(txn.headers['subject'][1],"%?=\r?\n\t=%?[^%? ]+%?[QqBb]%?","")
	end 
	redis:lpush('log', json.encode(log))
	redis:ltrim('log', 0, 99)

	-- Forward the message onward
	if smtp.sendfile({'127.0.0.1:26'}, txn.sender, txn.rcpts_accepted, client.msg_file) then
		return msg_action(string.format('250 message <%s> accepted', txn.id))
	else
		-- Error
		return msg_action('450 internal error', 'msg_error')
	end
end

function msg_action(msg,name)
	local code = tonumber(string.sub(msg,1,1))
	if code == 2 then
		-- ACCEPT
		conn.msgs_accepted = conn.msgs_accepted + 1
		redis:hincrby('stats:'..txn.date, 'msg_accept', 1)
		if name then
			redis:hincrby('stats:'..txn.date, 'msg_accept:'..name, 1)
		end
	elseif code == 4 then
		-- TEMPFAIL
		conn.msgs_tempfail = conn.msgs_tempfail + 1
		redis:hincrby('stats:'..txn.date, 'msg_tempfail', 1)
		if name then
			redis:hincrby('stats:'..txn.date, 'msg_tempfail:'..name, 1)
		end
	elseif code == 5 then
		-- REJECT
		conn.msgs_rejected = conn.msgs_rejected + 1
		redis:hincrby('stats:'..txn.date, 'msg_reject', 1)
		if name then
			redis:hincrby('stats:'..txn.date, 'msg_reject:'..name, 1)
		end
	else
		-- ERROR
		syslog.error(string.format('msg_action: unhandled code (%d)', code))
		redis:hincrby('stats:'..txn.date, 'msg_error', 1)
		return '450 Internal error'
	end

	local mid = ""
	if txn.headers and txn.headers['message-id'] then
		mid = txn.headers['message-id'][1]
	end

	-- Log point
	syslog.info(string.format(
		'message from="<%s>" nrcpts=%d mid="%s" size=%d dr="%s" x="%s"',
		txn.sender,
		#txn.rcpts_accepted or 0,
		mid,
		txn.size or 0,
		table_concat_keys(txn.dot_rejectables),
		msg
		)
	)
	return msg
end

function hook.noop()
	-- Count
	conn.noop_count = conn.noop_count + 1
	debug('hook.noop: count='..conn.noop_count)
	-- Check command case
	check_command_case(client.input)
end

function hook.help()
	debug('hook.help')
	-- Check command case
	check_command_case(client.input)
	return '214 2.0.0 Read the manual' 
end

function hook.rset()
	-- Count
	conn.rset_count = conn.rset_count + 1
	debug('hook.rset: count='..conn.rset_count)

	-- Check command case
	check_command_case(client.input)

	-- Check for extra parameters
	_,_,params = string.find(client.input, "^%w+ (.+)$")
	if params then
		debug('hook.rset found extra parameters: '..params)
	end
end

function hook.quit()
	-- Check command case
	check_command_case(client.input)

	-- Check for extra parameters
	_,_,params = string.find(client.input, "^%w+ (.+)$")
	if params then
		debug('hook.quit found extra parameters: '..params)
	end

	-- Note
	conn.clean_quit = true
	-- Send a multi-line reply because we're evil
	-- and this might detect some pipelining.
	quit_msg = 
		[[221-2.0.0 Why stop now just when I'm hating it?]].."\r\n"..
		[[221 2.0.0 %s closing connection]].."\r\n"
	return string.format(quit_msg, smtpe.host)
end

function hook.reply(reply)
	-- Byte counters
	if client.input then
		conn.bytes_in = conn.bytes_in + string.len(client.input)
	end
	conn.bytes_out = conn.bytes_out + reply:len()

	local code = reply:sub(1,1)
	if not conn.reply_stats[code] then
		conn.reply_stats[code] = 0
	end
	conn.reply_stats[code] = conn.reply_stats[code] + 1
	conn.reply_stats['total'] = conn.reply_stats['total'] + 1
	conn.last_reply = reply
	conn.last_reply_time = socket.gettime()
	if code == '5' then
		conn.last_error = reply:sub(1,reply:len()-2)
	end
end

function split_host_domain(name) 
	local offset = net.index_valid_tld(name)
	if offset == nil or offset <= 0 then return nil, nil end
	for i = offset-2, 1, -1 do
		if i == 1 then
			offset = 1
			break
		end
		if string.char(name:byte(i)) == '.' then
			offset = i+1
			break
		end
	end
	if 2 < offset then
		return name:sub(1, offset-2), name:sub(offset)
	end
	return nil, name:sub(offset)
end

function hostname_to_domain(host) 
	local host, domain = split_host_domain(host)
	return domain
end

function dns_list_lookup(name, lists)
	if (not name or #lists == 0) or (name and type(name) == 'table' and #name == 0) then
		return {}
	end
	local queries = {}
	local results = {}
	local query_to_name = {}
	local start_time = socket.gettime()
	dns.open()
	for n, list in pairs(lists) do
		-- Make sure that the list name ends in '.'
		if list:sub(-1) ~= '.' then
			-- Append dot
			lists[n] = list..'.'
			list = list..'.'
		end
		if type(name) == 'table' then
			-- Multiple items to look-up
			for _, name in ipairs(name) do
				-- TODO: ignore duplicates (e.g. identical queries)
				-- Skip IPv6 addresses
				if not string.find(name, ':') then
					local query = name..'.'..list
					query_to_name['IN,A,'..query] = name
					debug('dns_list_lookup: '..query)
					dns.query(dns.class.IN, dns.type.A, query)
					table.insert(queries, {["query"] = 'IN,A,'..query, ["list"] = list})
				end
			end
		else
			-- Regular string lookup
			if not string.find(name, ':') then	-- skip IPv6
				local query = name..'.'..list
				query_to_name['IN,A,'..query] = name
				debug('dns_list_lookup: '..query)
				dns.query(dns.class.IN, dns.type.A, query)
				table.insert(queries, {["query"] = 'IN,A,'..query, ["list"] = list})
			end
		end
	end
	-- Skip if we don't have any queries
	if not (#queries > 0) then
		dns.close()
		return results
	end
	local answers = dns.wait(1)
	for n, q in pairs(queries) do
		debug('dns_list_lookup: checking result: '..q.query)
		local result = answers[q.query]
		if result then
			local rcodename = dns.rcodename(result.rcode)
			-- Store list stats by list name and rcode
			redis:hincrby('dns-list-stat:'..q.list,rcodename,1)
			if rcodename == 'OK' then
				-- We got one or more hits; read the answer(s)
				for r, rr in ipairs(result.answer) do
					-- Discard any non-A records and make sure
					-- that the result is in the 127/8 range.
					if dns.typename(rr.type) == 'A' then
						if string.find(rr.value, '^127') then
							if not results[query_to_name[q.query]] then
								results[query_to_name[q.query]] = {}
							end
							if not results[query_to_name[q.query]][q.list] then
								results[query_to_name[q.query]][q.list] = {}
							end
							table.insert(results[query_to_name[q.query]][q.list], rr.value)
						end
					end
				end
				table.insert(results, q.list)
			end
		else
			syslog.error('missing result from DNS list lookup:'..q.list)
		end
	end
	dns.reset()
	dns.close()
	local end_time = socket.gettime()
	debug('dns_list_lookup: time elapsed = '..(end_time - start_time))
	return results
end

function dns_list_lookup_ip(ip, lists) 
	-- Exclude reserved IPs (they'll never be listed)
	if net.is_ip_reserved(ip, net.is_ip.RESTICTED) then
		return nil
	else
		local reverse_ip = net.reverse_ip(ip, 0)
		return dns_list_lookup(reverse_ip, lists)
	end
end

function check_command_case(cmd) 
	if string.find(cmd, ':') then
		_,_,cmdstr = string.find(cmd, '^([^:]+)')
	else
		_,_,cmdstr = string.find(cmd, '^([^: ]+)')
	end
	if cmdstr then 
		if string.find(cmdstr,'^[A-Z ]+$') then
			conn.flags['cmd_case_upper'] = true
		elseif string.find(cmdstr,'^[a-z ]+$') then
			conn.flags['cmd_case_lower'] = true
		else
			conn.flags['cmd_case_mixed'] = true
		end
	end
	-- Error?
	return nil
end

function check_for_pipelining(bool, is_esmtp)
	if bool and not is_esmtp then
		debug('pipelining detected: '..client.input)
		add_rejectable(conn.rejectables, {
			name = 'pipelining',
			output = '550 5.3.3 pipelining not allowed'
		})              
        end
	return nil      
end

function md5_hexdigest(string)
	local md5_obj = md5.new()
	md5_obj:append(string)
	return md5_obj:done()
end

function file_to_kv_table(file)
	local f = assert(io.open(file, 'r'))
	local t = {}
	for line in f:lines() do
		-- Skip # comments or -- comments or blank lines
		if not (line:find('^%s*#') or line:find('^%s*%-%-') or line:find('^%s*$')) then
			-- key = value format
			local _,_,key,value = line:find('^%s*([^= ]+)%s*=%s*([^=]+)$')
			if key and value then
				-- Make sure key is always lowercase
				key = key:lower()
				t[key] = value
			else
				-- value format
				local _,_,value = line:find('^%s*([^ ]+)$')
				if value then
					-- Make sure key is always lowercase
					value = value:lower()
					t[value] = true
				end
			end
		end
	end
	f:close()
	return t
end

function verify_rcpt(rcpt, host)
	-- TODO: allow multiple hosts
	-- TODO: record verified recipients
	local _,_,domain = string.find(rcpt, '@(.+)$')

	-- Check cache
	local host_key = 'dumb:'..host..','..domain
	local dumb = redis:get(host_key)
	if dumb == '2' or			-- Dumb host
	   redis:get('rcpt:'..rcpt) == '2'	-- Cached good
	then
		-- Accept
		return 2
	end

	if not dumb or dumb == '5' then
		-- Server rejects invalid users; or we haven't checked it yet
		local fake_rcpt = md5_hexdigest(rcpt)..'_'..rcpt
		local _,_,rcpt_result = smtp.sendfile({host}, txn.sender, {fake_rcpt, rcpt}, nil)
		if #rcpt_result > 0 then
			if rcpt_result[1]:sub(1,1) == '2' then
				-- Dumb host, cache dumb result and accept recipient
				redis:set(host_key, '2')
				redis:expire(host_key, 86400) -- try once per day.
				return 2
			elseif rcpt_result[1]:sub(1,1) == '5' then
				-- Host rejects invalid users, cache result
				if not dumb then redis:set(host_key, '5') end
				redis:expire(host_key, 86400) -- Touch expire time
				-- Check real recipient result
				if rcpt_result[2]:sub(1,1) == '5' then
					-- Invalid recipient; return output from host
					return 5
				elseif rcpt_result[2]:sub(1,1) == '2' then
					-- Valid, cache and accept
					redis:set('rcpt:'..rcpt, '2')
					redis:expire('rcpt:'..rcpt, (86400*7)) -- 7 days
					return 2
				end
			end
		end
	end

	-- Unknown result; tempfail
	return 4
end

function get_mx_list(domain) 
	if not domain then return nil end
	local mxes = {}
	local mx_not_found = {}
	dns.open()
	dns.query(dns.class.IN, dns.type.MX, domain..'.')
	dns.query(dns.class.IN, dns.type.A, domain..'.') -- Implicit MX
	local mx_result = dns.wait(1)
	dns.reset()
	local mx_answers = mx_result['IN,MX,'..domain..'.']
	if mx_answers.answer and #mx_answers.answer > 0 then
		for _,rr in ipairs(mx_answers.answer) do
			mx_not_found[rr.value] = true
			table.insert(mx_not_found, rr.value)
		end
	end
	local implicit = mx_result['IN,A,'..domain..'.']
	if #mx_not_found == 0 and (implicit.answer and #implicit.answer > 0) then
		-- Add implicit MX record
		mxes['domain'] = implicit.answer[1].value
	end
	-- Check 'extra' section
	if mx_answers.extra and #mx_answers.extra > 0 then
		for _,rr in ipairs(mx_answers.extra) do
			if dns.typename(rr.type) == 'A' then
				if mx_not_found[rr.name] then
					mxes[rr.name] = rr.value
					-- Unset as we've found it now..
					mx_not_found[rr.name] = nil
				end
			end
		end
	end
	local found_unresolved = false		 
	for mx,_ in pairs(mx_not_found) do
		if type(mx) ~= 'number' then
			dns.query(dns.class.IN, dns.type.A, mx)
			found_unresolved = true
		end
	end
	if found_unresolved then
		local mx_result = dns.wait(1)
		dns.reset()
		for mx,_ in pairs(mx_not_found) do
			local mx_answers = mx_result['IN,A,'..mx]
			if mx_answers and mx_answers.answer and #mx_answers.answer > 0 then
				mxes[mx] = mx_answers.answer[1].value
				mx_not_found[mx] = nil
			end
		end
	end
	dns.close()
	return mxes
end

function dns_list_lookup_uri(domains, lists)
	-- Run list queries
	local results = dns_list_lookup(domains, lists)
	if results and #results > 0 then
		for uri, uri_t in pairs(results) do
			if type(uri_t) == 'table' then
				for uri_list, uri_res in pairs(uri_t) do
					add_rejectable(txn.dot_rejectables, {
						name = 'uri_checks',
						sub_test = uri_list,
						output = string.format('550 Message rejected; URI \'%s\' blacklisted (%s)', uri, uri_list)
					})
				end
			end
		end
	end
	return nil
end

function lua_content_filters()
	if txn.headers['x-mimeole'] and #txn.headers['x-mimeole'] > 1 then
		add_rejectable(txn.dot_rejectables, {
			name = 'lua_content_filters',
			sub_test = 'MULTIPLE_XMIMEOLE',
			output = '550 Message rejected by content filtering'
		})
	end

	-- Check for obviously bogus SPF records 
	if txn.spf_record then
		if string.find(txn.spf_record, 'ip4:0.0.0.0/0') and string.find(txn.spf_record, '%?all') then
			add_rejectable(txn.dot_rejectables, {
				name = 'lua_content_filters',
				sub_test = 'SPF_BOGUS',
				output = '550 Message rejected by content filtering'
			})
		end
	end

	-- Check for from == to
	if not conn.flags['relay'] and routeGetHosts(txn.sender) then
		for _,rcpt in ipairs(txn.rcpts_accepted) do
			if rcpt == txn.sender then
				add_rejectable(txn.dot_rejectables, {
					name = 'lua_content_filters',
					sub_test = 'FROM_EQ_TO',
					output = '550 Message rejected by content filtering'
				})
			end
		end
	end	
end

function ternary(if_true, true_value, default)
	if if_true ~= nil then
		return true_value
	end
	return default
end

function table_concat_keys(t)
	local result = nil
	if type(t) == 'table' then
		for k,_ in pairs(t) do
			if type(k) ~= 'number' then
				if not result then
					result = k
				else
					result = result .. ',' .. k
				end
			end
		end
	end
	return result or ''
end

function trim_table_to_limit(t, limit)
	math.randomseed(os.time())
	while #t > tonumber(limit) do
		local rand = math.random(1, #t)
		t[t[rand]] = nil
		table.remove(t, rand)
	end
end

function add_rejectable(t, rejt)
	if (not t or (t and type(t) ~= 'table'))
	or (not rejt or (rejt and not rejt.name))
	then
		return nil
	else
		if rejt.sub_test and not t[rejt.name..':'..rejt.sub_test] then
			table.insert(t, rejt)
			t[rejt.name..':'..rejt.sub_test] = #t
			t[rejt.name] = #t
		elseif not t[rejt.name] then
			table.insert(t, rejt)
			t[rejt.name] = #t
		end
	end
end

function table_insert_nodup(t, v)
	if (t and type(t) == 'table') and v then
		if not t[v] then
			table.insert(t, v)
			t[v] = #t
		end
	end
end

function debug(text)
	if conf.debug and conf.debug == 1 then
		syslog.debug(text)
	end
end

function file_exists(path)
	local f = io.open(path, 'r')
	if f ~= nil then
		f:close()
		return true
	end
	return false
end

function split(str, delim)
	local vals = {}
	local word = nil
	-- Add trailing delim to catch the last value
	str = str .. delim
	for i=1, str:len() do
		char = str:sub(i,i)
		if char ~= delim then
			if word then
				word = word .. char
			else
				word = char
			end
		else
			if word then
				table.insert(vals, word)
				word = nil
			else
				-- line with no data
				break
			end
		end
	end
	return vals
end

-- Load URL shortener list
url_shorteners = file_to_kv_table('/tmp/url_shorteners.txt')
