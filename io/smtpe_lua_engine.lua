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

Things to implement:
:: Preferences
:: Header modifications
:: Message Size
:: Rate Limiting
:: Relaying
:: Forwarding
:: BCC-style functions
:: Click whitelisting
:: Electronic Watermarks (EMEW)
:: Access-map whitelist/blacklist
:: OSBF-Lua
:: SPF
:: Filename/Filetype rules
:: ZIP/RAR Filename rules
:: Charset rules
:: Stats

]]--

local conf = {
	['quarantine_path'] = '/var/spool/smtpe/quarantine',
	['greylist_delay'] = 900,
	['greylist_pass_ttl'] = (86400*40),  -- 40 days
	['greylist_key_ttl'] = 90000,  -- 25 hours
	['abook_ttl'] = (86400*40), -- 40 days
	['helo_ttl'] = (86400*40), -- 40 days
	['spamd_score_reject'] = 10,
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
	['debug'] = 0,
}

local domains = {
	['fsl.com'] = '127.0.0.1:26',
	['d-rr.com'] = '127.0.0.1:26',
	['redcrane.net'] = '127.0.0.1:26',
	['dlsi.com'] = '127.0.0.1:26',
	['snertsoft.com'] = 'mx.snert.net',
	['snert.com'] = 'mx.snert.net',
	['vm2.fsl.com'] = '127.0.0.1:26',
}

function init_conn_table()
	conn = nil
	conn = {
		ptrs = {},
		host_id = nil,
		helo = nil,
		esmtp = false,
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
	}
	conn.start_time = socket.gettime()
end

function init_txn_table()
	txn = nil
	txn = {
		date = nil,
		sender = nil,
		rcpts = {},
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
	}
	-- Add date in YYYYMMDD format to transaction table
	-- This is used when messages are quarantined.
	local date = os.date('!*t')  -- NOTE: UTC
	txn.date = date.year .. string.format('%02d', date.month) .. string.format('%02d', date.day)
end

--print ('CPU count: '..util.cpucount())
--print (table.show(util.getloadavg(),'loadavg'))

function hook.accept(ip, ptr)
	debug('hook.accept: ip='..ip..', ptr='..ptr)

	-- rate/concurrency limits here

	-- Initialisation
	init_conn_table()
	init_txn_table()
 
	------------------------
	-- Access map lookups --
	------------------------
	-- TODO: ACCESS MAP LOOKUPS HERE

	------------------------
	-- Earlytalker checks --
	------------------------
	if client.is_pipelining then
		debug('hook.accept: earlytalker detected!')
		conn.flags['earlytalker'] = true
		table.insert(conn.rejectables, {
			name = 'earlytalker',
			output = '550 5.3.3 pipelining not allowed'
		})
	end

	-----------------------
	-- PTR record checks --
	-----------------------
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
				conn.flags['ptr_lookup_error'] = true
				table.insert(conn.rejectables, {
					name = 'no_ptr',
					output = '450 Error resolving rDNS name for host '..ip..' ('..dns.rcodename(result.rcode)..')'
				})
			else
				conn.flags['no_ptr'] = true
				table.insert(conn.rejectables, {
					name = 'no_ptr',
					output = '550 Host '..ip..' has no rDNS name'
				})
			end 
		else 
			for i, answer in ipairs(result.answer) do
				answer.value = answer.value:lower()
				table.insert(conn.ptrs,answer.value)
				-- is PTR valid TLD?
				if not net.has_valid_tld(answer.value) then
					conn.flags['ptr_invalid_tld'] = true
					table.insert(conn.rejectables, {
						name = 'ptr_invalid_tld',
						output = '550 Reverse DNS name for '..ip..' does not have a valid TLD ('..answer.value..')'
					})
				end 
				-- IP in name?
				if net.is_ipv4_in_name(ip, answer.value) > 0 then
					conn.flags['ip_in_ptr'] = true
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
				-- PTR multidomain
				if #conn.ptrs > 1 and
				   hostname_to_domain(answer.value) ~= hostname_to_domain(conn.ptrs[1])
				then
					 conn.flags['ptr_multidomain'] = true	
				end
			end
			local result = nil
			dns.reset()
		end
		dns.close()
	end

	-- Calculate the host ID for use with greylisting
	if conn.flags['no_ptr'] or 
	   conn.flags['ptr_lookup_error'] or
	   conn.flags['ptr_multidomain'] or 
	   conn.flags['ptr_invalid_tld'] or
	   conn.flags['ip_in_ptr'] 
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

	debug('host_id: '..conn.host_id)

	-- Check to see if this host has passed greylisting
	if redis:get('grey-pass:'..conn.host_id) then
		conn.flags['passed_greylist'] = true
	end

	-- TODO: lookup conn.ptrs in URI/Domain blacklists.

	----------------------
	-- DNS list lookups --
	----------------------
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
			conn.flags['dns_bl'] = true
			for i, list in ipairs(bl) do
				table.insert(conn.rejectables, {
					name = 'dns_bl',
					sub_test = list,
					output = '550 5.7.0 Host '..ip..' black listed by '..list
				})
			end
		end
	end
	-- TODO: Have we seen this host before (rep=?)

	-- Log point
	syslog.info(string.format('start i=%s p="%s", f="%s"', ip, ptr, table.concat(conn.flags)))

	-----------------
	-- SMTP Banner --
	-----------------
	local banner
	-- Send a single line banner if the host is known good
	banner = string.format('220 %s BarricadeMX ESMTP engine (%s)', smtpe.host, client.id_sess)
	-- TODO: expand this list
	if net.is_ip_reserved(ip, (net.is_ip.LAN + net.is_ip.LOCAL)) or 
	   conn.flags['dns_wl'] or 
	   conn.flags['dns_yl'] or
	   conn.flags['passed_greylist']
	then
		return banner
	end
	-- Send a multi-line banner to attempt to confuse some SMTP clients
	-- We also check for input between each line to catch earlytalkers.
	banner = string.format(
		[[220-%s BarricadeMX SMTP engine (%s)]].."\r\n"..
		[[220-Spam is not welcome, accepted or tolerated here. You have been warned...]].."\r\n"..
		[[220 http://www.fsl.com/barricademx]],
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
	if txn.quarantine and client.msg_file then
		local new_path = conf.quarantine_path .. '/' .. txn.date
		if txn.quarantine_type then
			new_path = new_path .. '/' .. txn.quarantine_type
		end
		-- Create path if necessary
		local bool = util.mkpath(new_path)
		-- Add in transaction ID
		new_path = new_path .. '/' .. client.id_trans;
		local success, err = os.rename(client.msg_file, new_path);
		debug('quarantined message: path='..new_path)
		if success then
			-- TODO: store pointer in datastore?
		else
			syslog.error('error quarantining file: '..err)
		end
	end

	-- Reset our transaction state
	if txn.sender then
		-- print(table.show(txn))
		conn.txn_count = conn.txn_count + 1
	end
	init_txn_table()
end

function hook.ehlo(arg)
	debug('hook.ehlo: arg='..arg)

	-- Check for pipelining
	if not conn.flags['pipelining'] and client.is_pipelining 
	then
		debug('hook.ehlo: pipelining detected!')
		conn.flags['pipelining'] = true
		table.insert(conn.rejectables, {
			name = 'pipelining',
			output = '550 5.3.3 pipelining not allowed'
		})
	end

	-- Check command case
	local case = check_command_case(client.input)
	if case then 
		conn.flags[case] = true;
	end

	-- Store
	conn.helo = arg
	-- TODO: Allow localhost, relays, known servers
	if client.address == '127.0.0.1' then
		conn.esmtp = true
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
			table.insert(conn.rejectables, {
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
				table.insert(conn.rejectables, {
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
				table.insert(conn.rejectables {
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
						conn.flags['helo_matches_ptr'] = true;
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
				if tld == 'localdomain' and net.is_ip_reserved(client.address, net.is_ip.LOCAL) then
					return nil
				elseif tld == 'lan' then
					return nil
				elseif tld == 'local' then
					return nil
				else 
					table.insert(conn.rejectables, {
						name = 'strict_helo',
						sub_test = 'invalid_tld',
						output = '550 Invalid HELO; domain contains invalid TLD ('..tld..')'
					})
					return nil
				end
			end -- if tld
		else 
			table.insert(conn.rejectables, {
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
		table.insert(conn.rejectables, {
			name = 'strict_helo',
			sub_test = 'not_dots',
			output = '550 Invalid HELO; argument must be host/domain or address literal'
		})
		return nil
	end -- if string.find(arg, '%.')
	-- Catch all..
	table.insert(conn.rejectables, {
		name = 'strict_helo',
		sub_test = 'unknown',
		output = '550 Invalid HELO'
	})
end

function hook.helo(arg)
	debug('hook.helo: arg='..arg)
	conn.ehlo_no_helo = false;

	-- Check for pipelining
	if not conn.flags['pipelining'] and client.is_pipelining then 
		debug('hook.helo: pipelining detected!')
		conn.flags['pipelining'] = true
		table.insert(conn.rejectables, {
			name = 'pipelining',
			output = '550 5.3.3 pipelining not allowed'
		})
	end

	-- Check command case
	local case = check_command_case(client.input)
	if case then
		conn.flags[case] = true;
	end

	-- Check for schizophrenia
	if conn.helo and conn.helo ~= arg then
		conn.flags['helo_schizophrenic'] = true;
		table.insert(conn.rejectables, {
			name = 'helo_schizophrenic',
			sub_test = 'short_term',
			output = '550 5.7.1 Host '..client.host..' is schizophrenic (1)'
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
			table.insert(conn.rejectables, {
				name = 'helo_schizophrenic',
				sub_test = 'long_term',
				output = '550 5.7.1 Host '..client.host..' is schizophrenic (2)'
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
	local case = check_command_case(client.input)
	if case then
		conn.flags[case] = true;
	end

	-- TODO: implement AUTH proxy
	-- TODO: how to report capabilities?
end

function hook.unknown(input)
	-- TODO: Check for pipelining

	-- Check command case
	local case = check_command_case(client.input)
	if case then
		conn.flags[case] = true;
	end

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
		-- TODO: STAT: buffer overflow in engine
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
	debug('hook.unknown: arg='..input..', count='..conn.unknown_count)
end

function hook.mail(sender) 
	debug('hook.mail: sender='..sender..' id='..client.id_sess)

	-- Check for pipelining
	if not conn.flags['pipelining'] and (client.is_pipelining and not conn.esmtp) then
		debug('hook.mail: pipelining detected!')
		conn.flags['pipelining'] = true
		table.insert(conn.rejectables, {
			name = 'pipelining',
			output = '550 5.3.3 pipelining not allowed'
		})
	end

	-- Record data
	txn.sender = sender:lower()

	-- Check command case
	local case = check_command_case(client.input)
	if case then
		conn.flags[case] = true;
	end

	-- Check for extra spaces
	if string.find(client.input, ': <') then
		conn.flags['cmd_mail_extra_spaces'] = true
		txn.flags['cmd_mail_exta_spaces'] = true
		debug('hook.mail: found extra spaces')
	end

	-- Check for extra parameters
	_,_,params = string.find(client.input, '> (.+)$')
	if params then
		debug('hook.mail: extra parameters found: '..params)
		-- Check that we are in SMTP mode...
		if not conn.esmtp then
			conn.flags['cmd_mail_extra_params'] = true
			txn.flags['cmd_mail_extra_params'] = true
			table.insert(txn.rejectables, {
				name = 'mail_extra_params',
				output = '555 Unsupported MAIL parameters sent'
			})
		else
			-- Split parameters into words
			for param in params:gmatch('([^ ]+)') do
				local match = false
				-- Check parameter pairs
				for k, v in param:gmatch('([^= ]+)=([^= ]+)') do
					if string.upper(k) == 'SIZE' then
						txn.size = v
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
					table.insert(txn.rejectables, {
						name = 'mail_extra_params',
						output = '555 Unsupported MAIL parameter \''..param..'\''
					})
				end
			end
		end
	end

	-- Strip domain
	local _,_,domain = string.find(txn.sender, '@(.+)')
	if domain and not domains[domain] then    -- Don't run these check for our own domains!
		debug('hook.mail: found domain: '..domain)
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
			local rcodename = dns.rcodename(mx_answer.rcode)
			if rcodename == 'OK' then
				local mx_count = 0
				for host, ip in pairs(get_mx_list(domain)) do
					-- Client is MX?
					if client.address == ip then
						conn.flags['client_is_mx'] = true;
					end
					mx_count = mx_count + 1
				end
				-- Did we find any valid MX records that correctly resolved?
				if mx_count == 0 then
					table.insert(txn.rejectables, {
						name = 'mail_mx_required',
						output = '550 Domain \''..domain..'\' does not have any valid MX records'
					}) 
				end	
			elseif rcodename == 'NXDOMAIN' then
				table.insert(txn.rejectables, {
					name = 'mail_mx_required',
					output = '550 Domain \''..domain..'\' does not resolve'
				})
			else
				-- Some other DNS error = TEMPFAIL
				table.insert(txn.rejectables, {
					name = 'mail_mx_required',
					output = '451 Error resolving MX records for domain \''..domain..'\' ('..dns.rcodename(mx_result.rcode)..')'
				})	
			end
		else
			-- Unknown DNS result = TEMPFAIL
			table.insert(txn.rejectables, {
				name = 'mail_mx_required',
				output = '451 Error resolving MX records for domain \''..domain..'\''
			})
		end
		-- TODO: SPF
	end

	-- One domain per connection
	-- TODO: exclude localhost, relays etc.
	if domain and conn.domain ~= domain then
		table.insert(conn.rejectables, {
			name = 'single_domain_per_conn',
			output = '450 Only one sender domain per connection is allowed from your host'
		})
	end

	-- TODO: strict 'freemail' e.g. from domain must appear in rDNS
end

function hook.rcpt(rcpt)
	debug('hook.rcpt: rcpt='..rcpt)
	
	-- Check for pipelining
	if not conn.flags['pipelining'] and (client.is_pipelining and not conn.esmtp) then
		debug('hook.rcpt: pipelining detected!')
		conn.flags['pipelining'] = true
		table.insert(conn.rejectables, {
			name = 'pipelining',
			output = '550 5.3.3 pipelining not allowed'
		})
	end
	
	-- Check command case
	local case = check_command_case(client.input)
	if case then
		conn.flags[case] = true;
	end

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
		if not conn.esmtp then
			conn.flags['cmd_rcpt_extra_params'] = true
			txn.flags['cmd_rcpt_extra_params'] = true
			table.insert(txn.rejectables, {
				name = 'rcpt_extra_params',
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
					table.insert(txn.rejectables, {
						name = 'rcpt_extra_params',
						output = '555 Unsupported RCPT parameter \''..param..'\''
					})
				end
			end
		end
	end

	-- One recipient per null
	if (txn.sender and txn.sender == '') and #txn.rcpts > 0 then
		table.insert(txn.rejectables, {
			name = 'one_rcpt_per_null',
			output = '550 Only one recipient is allowed for messages with a null reverse path'
		})
	end

	-- Check message size; might have been passed
	-- as SIZE= parameter to MAIL command.
	if tonumber(txn.size) > 0 then
		-- TODO:
	end

	-- Is the message to one of our domains?
	local _,_,domain = string.find(rcpt, '@(.+)$')
	if domains[domain] then
		-- Inbound message

		-- Verify recipient
		local verify = verify_rcpt(rcpt)
		if verify == 5 then
			return '550 recipient <'..rcpt..'> unknown'
		elseif not verify or verify == 4 then
			return '450 recipient <'..rcpt..'> verification error'
		end

		-- Apply any whitelisting
		if conn.flags['dns_wl'] or
		   conn.flags['dns_gl'] or
		   redis:sismember('abook:'..rcpt:lower(), txn.sender)  -- AWL
		then
			return rcpt_accept(rcpt)
		end

		-- Greylisting
		local grey_key = 'grey:'..conn.host_id..','..txn.sender..','..rcpt:lower()
		debug('greylisting: key='..grey_key)
conn.flags['passed_greylist'] = true
		if not conn.flags['passed_greylist'] then
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
					return '451 recipient <'..rcpt..'> greylisted for '..time_left..' seconds'
				end
			else
				-- Create greylist record
				redis:set(grey_key, time_now)
				redis:expire(grey_key, conf.greylist_delay)
				return '451 recipient <'..rcpt..'> greylisted for '..conf.greylist_delay..' seconds'
			end
		else
			-- Tidy-up any remaining greylist keys
			redis:del(grey_key)
			-- Update key expiry time on pass record
			redis:expire('grey-pass:'..conn.host_id, conf.greylist_pass_ttl)
		end
	else
		-- Outbound message

		-- Is this host allowed to relay?
		-- TODO: lookup relay
		if not net.is_ip_reserved(client.address, net.is_ip.LOCAL) then
			return '550 <'..rcpt..'> relaying denied'
		end

		-- Strict relay rules?
		local _,_,sdomain = string.find(txn.sender,'@(.+)$')
		-- We only allow outbound domains that we allow inbound
		if sdomain and not domains[sdomain] then
			return '550 <'..rcpt..'> relaying denied from domain \''..sdomain..'\''
		end

		txn.flags['outbound'] = true
	end

--[[
	-- Apply any rejectables
	if conn.rejectables and #conn.rejectables > 0 then
		if string.sub(conn.rejectables[1].output,1,1) == '4' then
			conn.rcpts_tempfail = conn.rcpts_tempfail + 1
		else
			conn.rcpts_rejected = conn.rcpts_rejected + 1
		end
		return conn.rejectables[1].output
	end
	if txn.rejectables and #txn.rejectables > 0 then
		if string.sub(txn.rejectables[1].output,1,1) == '4' then
			conn.rcpts_tempfail = conn.rcpts_tempfail + 1
		else
			conn.rcpts_rejected = conn.rcpts_rejected + 1
		end
		return txn.rejectables[1].output
	end
]]--

	-- If we get to here, then accept the recipient
	return rcpt_accept(rcpt)
end

function rcpt_accept(rcpt)
	-- Add recipient to valid recipient table and accept
	table.insert(txn.rcpts, rcpt:lower())
	conn.rcpts_accepted = conn.rcpts_accepted + 1
	return '250 2.1.0 recipient <'..rcpt..'> OK'
end

function hook.data()
	debug('hook.data')

	-- Check for pipelining
	if not conn.flags['pipelining'] and (client.is_pipelining and not conn.esmtp) then
		debug('hook.data: pipelining detected!')
		conn.flags['pipelining'] = true
		table.insert(conn.rejectables, {
			name = 'pipelining',
			output = '550 5.3.3 pipelining not allowed'
		})
	end

	-- Check command case
	local case = check_command_case(client.input)
	if case then
		conn.flags[case] = true;
	end
	-- Check for extra parameters
	_,_,params = string.find(client.input, "^%w+ (.+)$")
	if params then
		debug('hook.data found extra parameters: '..params)
	end

	-- TODO: Preferences
	for i, rcpt in ipairs(txn.rcpts) do
		local _,_,domain = rcpt:find('@(.+)$')
		if domain and not txn.rcpt_domains[domain] then
			txn.rcpt_domains[domain] = 1
			table.insert(txn.rcpt_domains, domain)
		end
	end
	
	-- Work out what level of post-DATA preferences can be used
	if #txn.rcpts == 1 then
		txn.prefs = txn.rcpts[1];
	elseif #txn.rcpt_domains == 1 then
		-- Domain
		txn.prefs = txn.rcpt_domains[1]
	else
		-- Global
		txn.prefs = 'GLOBAL'
	end
	syslog.info('post-DATA preferences: '..txn.prefs)
end

function hook.header(line)
	_,_,name,value = string.find(line, "^([^ ]+):%s+(.+)")
	if name and value then
		debug('found header: header='..name..', value='..value)
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
		table.insert(txn.dot_rejectables, {
			name = 'rfc5322_min_headers',
			sub_test = 'date_required',
			output = '550 Message rejected; no \'Date\' header found'
		})
	end
	if not txn.headers['from'] or (txn.headers['from'] and not txn.headers['from'][1]) then
		table.insert(txn.dot_rejectables, {
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
		table.insert(txn.dot_rejectables, {
			name = 'rfc5322_max_headers',
			output = '550 Message rejected; contains non-unique message headers that should be unique'
		})
	end

	-- Already marked as spam?
	if txn.headers['x-spam-flag'] and string.match(txn.headers['x-spam-flag'][1], '[Yy][Ee][Ss]$') then
		table.insert(txn.dot_rejectables, {
			name = 'already_spam',
			output = '550 Message rejected; already marked as spam by an upstream host'
		})
	end

	local _,_,domain = string.find(txn.sender, '@(.+)$')
	if txn.flags['outbound'] and (domain and domains[domain]) then	-- Only for our inbound domains
		-- TODO: Electronic watermarks

		-- Addressbook auto-whitelist
		-- Only add to addressbook if message was not auto-generated
		if (txn.headers['auto-submitted'] and string.lower(txn.headers['auto-submitted'][1]) ~= 'no') or
		   (txn.headers['precedence'] and 
		     (string.format(txn.headers['precedence'][1]) ~= 'bulk' or
		      string.format(txn.headers['precedence'][1]) ~= 'list' or
		      string.format(txn.headers['precedence'][1]) ~= 'junk')
		   )
                then
			for _, rcpt in ipairs(rcpts) do
				redis:sadd('abook:'..txn.sender, rcpt)
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
		smtpe.host, client.local_address, txn.sender, ternary(conn.esmtp, 'ESMTP', 'SMTP'), 
		client.id_trans, 'none', os.date('%a, %d %b %Y %H:%M:%S %z'))
	, 1)
end

function hook.body(line)
	-- Not used at this time
end

function hook.dot()
	debug('hook.dot')

	-- Check message (excluding headers) length

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
					table.insert(txn.mime_types, mtype)
				end
				-- Filename
				local _,_,file = string.find(part.content_type,'[Nn][Aa][Mm][Ee]="?([^;"]+)"?')
				if file then
					table.insert(txn.filenames, file)
					file = file:lower()
					debug('Found attachment: '..file)
					local _,_,extn = file:find('%.([^%.]+)$')
					if extn and not txn.fileextns[extn] then
						table.insert(txn.fileextns, extn)
						txn.fileextns[extn] = 1
						debug('Found attached file extension: '..extn)
					end
					-- TODO: dispositions
				end
				-- Charset
				local _,_,charset = string.find(part.content_type:lower(),'charset="?([^;"]+)"?')
				if charset and not txn.charsets[charset] then
					table.insert(txn.charsets, charset)
					txn.charsets[charset] = 1
					debug('Found charset: '..charset)
				end
			end
		end
	end
	-- Charsets
	-- Get other potential charsets from Content-Type and Subject headers
	if txn.headers['content-type'] then
		local _,_,charset = string.find(string.lower(txn.headers['content-type'][1]), 'charset="?([^;"]+)"?')
		if charset and not txn.charsets[charset] then
			table.insert(txn.charsets, charset)
			txn.charsets[charset] = 1
			debug('Found charset: '..charset)
		end
	end
	if txn.headers['subject'] then
		local _,_,charset = string.find(string.lower(txn.headers['subject'][1]), '^=%?([^%s]+)%?[BQ]%?')
		if charset and not txn.charsets[charset] then
			table.insert(txn.charsets, charset)
			txn.charsets[charset] = 1
			debug('Found charset: '..charset)
		end
	end

	-- TODO: Message size
	-- TODO: Filename/Filetype checks

	lua_content_filters()

	-- TODO: URIBL/Domain BL
	-- TODO: Custom Bayes?
	-- TODO: OSBF-Lua

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
				if not uri_hosts_to_check[uri_host] then
					table.insert(uri_hosts_to_check, uri_host)
					uri_hosts_to_check[uri_host] = #uri_hosts_to_check
				end
				if uri_domain and not uri_domains_to_check[uri_domain] then
					table.insert(uri_domains_to_check, uri_domain)
					uri_domains_to_check[uri_domain] = #uri_domains_to_check
				end
				if current_uri.scheme == 'mailto' and not uri_mailto_to_check[uri_raw] then
					table.insert(uri_mailto_to_check, uri_raw)
					uri_mailto_to_check[uri_raw] = #uri_mailto_to_check
				end
				if uri_domain and (url_shorteners[uri_domain] and (current_uri.path and current_uri.path ~= '/') and not uri_shortened_to_check[uri_raw]) then
					table.insert(uri_shortened_to_check, current_uri.uri_raw)
					uri_shortened_to_check[current_uri.uri_raw] = #uri_shortened_to_check
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

	-- Run list queries
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
						syslog.info('Found shortened URL: '..http.url..' -> '..location)
						-- TODO: check for ToS violation pages
						local parsed_uri = uri.parse(location)
						local uri_host = string.lower(parsed_uri.host)
						local uri_domain = string.lower(hostname_to_domain(uri_host))
						-- Make sure this isn't another shortener
						if not url_shorteners[uri_domain] then
							if not uri_hosts_to_check[uri_host] then
								table.insert(uri_hosts_to_check, uri_host)
                        	       	         		uri_hosts_to_check[uri_host] = #uri_hosts_to_check
							end
							if not uri_domains_to_check[uri_domain] then
								table.insert(uri_domains_to_check, uri_domain)
								uri_domains_to_check[uri_domain] = #uri_domains_to_check
							end
						else
							-- Make sure it's not a redirection to the *same* shortener.
							local check = uri.parse(http.url)
							if not uri_domain == string.lower(check.host) then
								-- TODO: found shortener chain
								syslog.info('Found shortener chain: '..check.uri_raw..' -> '..location)
								-- What to do here... recurse to a certain level?
								-- Reject the message? (apparently some geniune cases...)
							end
						end
					end
				elseif http.rcode == 404 then
					-- TODO: resolves to a 404 page.
				else
					syslog.error('URI shortener returned unhandled rcode ('..http.rcode..') for URI '..http.url)
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
				table.insert(txn.dot_rejectables, {
					name = 'virus',
					sub_test = 'clamd',
					output = '550 Message rejected; infected with virus \''..virus..'\''
				})
					
			end
			syslog.info(string.format('clamd: elapsed=%.2f host="%s" result="%s"', sres.clamd.elapsed_time, sres.clamd.service_host, result))
		else
			syslog.error('error parsing clamd result')
		end	
	end

	-- spamd
	-- TODO: limit message size sent to spamd
	-- TODO: optionally only send text/* and message/* parts to spamd
	if sres['spamd'] then
		-- Parse reply
		local found,_,is_spam,score,threshold = sres.spamd.reply:find("Spam:%s+([^%s]+)%s+;%s+([%d.]+)%s+/%s+([%d.]+)\r?\n")
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
					table.insert(txn.dot_rejectables, {
						name = 'spamd',
						output = '550 Message rejected; spam score ('..score..') exceeds threshold'
					})
				else
					-- TODO: expand header additions
					header.add('X-Spam-Flag: YES')
					-- Modify subject
					local sub_idx, subject = header.find('subject', 1)
					if subject then
						header.modify('Subject', 1, '[SPAM] '..subject)
					end
					-- Set 'Precedence' header to junk (or add if not already present)
					header.modify('Precedence', 1, 'junk')
				end
			end
			syslog.info(string.format('spamd: elapsed=%.2f host="%s" score=%.1f tests="%s"', sres.spamd.elapsed_time, sres.spamd.service_host, score, table_concat_keys(txn.spamd_rules_hit))) 		
		else
			syslog.error('error parsing spamd output')
		end
	end

	-- Quarantine
	txn.quarantine = true 
	-- txn.quarantine_type = 'nonspam'

	-- hook.forward returns response
end

function hook.error(errno, error_text)
	-- Connection timed out (110)
	if errno == 110 then
		conn.timed_out = true
	else 
		conn.conn_error = true
	end
	debug('hook.error: errno:'..errno..', error_text='..error_text)
end

function hook.close(was_dropped)
	debug('hook.close: was_dropped='..was_dropped)
	if was_dropped and was_dropped == 1 then
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
	--print (table.show(conn,'conn'))
	--if txn.sender then print (table.show(txn,'txn')) end

	-- Log point
	syslog.info(
		string.format(
			'end i=%s p="%s" f="%s" rcpts=(a=%d,t=%d,r=%d) msgs=(a=%d,t=%d,r=%d) ct=%.2f', 
			client.address, 
			client.host, 
			table_concat_keys(conn.flags), 
			conn.rcpts_accepted, 
			conn.rcpts_tempfail, 
			conn.rcpts_rejected, 
			conn.msgs_accepted, 
			conn.msgs_tempfail, 
			conn.msgs_rejected, 
			conn.total_time
		)
	)	
end

function hook.forward(path, sender, rcpts)
	debug('hook.forward: path='..path)

	-- Reject message
	if txn.dot_rejectables and #txn.dot_rejectables > 0 then
		-- Count
		if string.sub(txn.dot_rejectables[1].output, 1, 1) == '4' then
			conn.msgs_tempfail = conn.msgs_tempfail + 1
		else 
			conn.msgs_rejected = conn.msgs_rejected + 1
		end
		return txn.dot_rejectables[1].output
	end

	-- Record message details
	local log = {
		["date"] = os.time(os.date('!*t')),
		["clientip"] = client.address,
		-- Client PTR
		-- Session ID
		-- Transaction ID
		-- 
		["from"] = txn.sender,
		["rcpts"] = txn.rcpts,
		["size"] = nil,

	}
	-- Add in subject header
	if txn.headers['subject'] and #txn.headers['subject'] > 0 then
		log["subject"] = txn.headers['subject'][1]
	end 
	redis:lpush('log', json.encode(log))
	redis:ltrim('log', 0, 99)

	-- Sink
	conn.msgs_accepted = conn.msgs_accepted + 1
	return '250 message '..client.id_trans..' accepted'
end

function hook.noop()
	-- Count
	conn.noop_count = conn.noop_count + 1
	debug('hook.noop: count='..conn.noop_count)
	-- Check command case
	local case = check_command_case(client.input)
	if case then
		conn.flags[case] = true;
	end
end

function hook.help()
	debug('hook.help')
	-- Check command case
	local case = check_command_case(client.input)
	if case then
		conn.flags[case] = true;
	end
	return '214 2.0.0 Read the fucking manual you cunt!' 
end

function hook.rset()
	-- Count
	conn.rset_count = conn.rset_count + 1
	debug('hook.rset: count='..conn.rset_count)

	-- Check command case
	local case = check_command_case(client.input)
	if case then
		conn.flags[case] = true;
	end

	-- Check for extra parameters
	_,_,params = string.find(client.input, "^%w+ (.+)$")
	if params then
		debug('hook.rset found extra parameters: '..params)
	end
end

function hook.quit()
	-- Check command case
	local case = check_command_case(client.input)
	if case then
		conn.flags[case] = true;
	end

	-- Check for extra parameters
	_,_,params = string.find(client.input, "^%w+ (.+)$")
	if params then
		debug('hook.quit found extra parameters: '..params)
	end

	-- Note
	conn.clean_quit = true
	-- Send a multi-line reply because we're evil
	-- This might detect some pipelining...
	quit_msg = 
		[[221-2.0.0 Why stop now just when I'm hating it?]].."\r\n"..
		[[221 2.0.0 %s closing connection]].."\r\n"
	return string.format(quit_msg, smtpe.host)
end

function hook.reply(reply)
	conn.bytes_out = conn.bytes_out + reply:len()
	if client.input then
		redis:hmset('conn:'..client.id_sess, 
			'lc', client.input,
			'idle', socket.gettime()
		)
		conn.bytes_in = conn.bytes_in + client.input:len()
	end
	local code = reply:sub(1,1)
	if not conn.reply_stats[code] then
		conn.reply_stats[code] = 0
	end
	conn.reply_stats[code] = conn.reply_stats[code] + 1
	conn.reply_stats['total'] = conn.reply_stats['total'] + 1
	conn.last_reply = reply
	conn.last_reply_time = socket.gettime()
	if code == '5' then
		conn.last_error = reply
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
			return 'cmd_case_upper'
		elseif string.find(cmdstr,'^[a-z ]+$') then
			return 'cmd_case_lower'
		else
			return 'cmd_case_mixed'
		end
	end
	-- Error?
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

function verify_rcpt(rcpt)
	local _,_,domain = string.find(rcpt, '@(.+)$')

	-- Check cache
	local host_key = 'dumb:'..domains[domain]..','..domain
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
		local _,_,rcpt_result = smtp.sendfile({domains[domain]}, txn.sender, {fake_rcpt, rcpt}, nil)
		if #rcpt_result > 0 then
			if rcpt_result[1]:sub(1,1) == '2' then
				-- Dumb host, cache dumb result and accept recipient
				redis:set(host_key, '2')
				redis:expire(host_key, 86400) -- try once per day.
				return 2
			elseif rcpt_result[1]:sub(1,1) == '5' then
				-- Host rejects invalid users, cache result
				if not dumb then redis:set('dumb:'..host_key, '5') end
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
					table.insert(txn.dot_rejectables, {
						name = 'uri_checks',
						sub_test = uri_list,
						output = '550 Message rejected; URI '..uri..' blacklisted ('..uri_list..')'
					})
				end
			end
		end
	end
	return nil
end

function lua_content_filters()
	if txn.headers['x-mimeole'] and #txn.headers['x-mimeole'] > 1 then
		table.insert(txn.dot_rejectables, {
			name = 'lua_content_filters',
			sub_test = 'MULTIPLE_XMIMEOLE',
			output = '550 Message rejected by content filtering'
		})
	end
end

function ternary(if_true, true_value, default)
	if if_true then
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

function debug(text)
	if conf.debug and conf.debug == 1 then
		syslog.debug(text)
	end
end

-- Load URL shortener list
url_shorteners = file_to_kv_table('/tmp/url_shorteners.txt')
