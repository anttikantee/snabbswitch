module(..., package.seeall)

local ffi = require("ffi")
local C = ffi.C

ffi.cdef[[
void free(void *);
]]

local app = require("core.app")
local packet = require("core.packet")
local buffer = require("core.buffer")
local pcap = require("apps.pcap.pcap")
local lib = require("core.lib")

require("apps.rumpkernel.snabbif_h")

rumpkernel = {}

function rumpkernel:new()
   local me = {}

   -- bootstrap rump kernel
   assert(C.rump_init() == 0)

   -- make the rump kernel listen to remote syscall requests,
   -- but first remove any old and stale control sockets that may exist
   os.remove("apps/rumpkernel/rumpkernctrl")
   assert(C.rump_init_server("unix://" .. "apps/rumpkernel/rumpkernctrl") == 0)

   -- configure it
   os.execute("sh apps/rumpkernel/configrouter.sh apps/rumpkernel/rumpkernctrl")

   return setmetatable(me, {__index = rumpkernel})
end

function rumpkernel:push()
   for ifname, iport in pairs(self.input) do
      for i = 1, app.nreadable(iport) do
	 local p = app.receive(iport)
	 C.snabbif_push(ifname, p.iovecs[0].buffer.pointer, p.length)
      end
   end
end

function rumpkernel:pull()
   for ifname, oport in pairs(self.output) do
      local dptr = ffi.new("void *[1]")
      local dlen = ffi.new("size_t[1]")

      while C.snabbif_pull(ifname, dptr, dlen) == 1 do
	 local p = packet.allocate()
	 local b = buffer.allocate()

	 ffi.copy(b.pointer, dptr[0], dlen[0])
	 packet.add_iovec(p, b, dlen[0], 0)
	 app.transmit(oport, p)
         -- XXXabstraction
         C.free(dptr[0])
      end
   end
end

function selftest ()
   print("selftest: rump kernel snabb switch ipv6 router")

   app.apps.rumpkernel = app.new(rumpkernel:new())
   app.apps.source = app.new(pcap.PcapReader:new("apps/rumpkernel/selftest.cap.input"))
   app.apps.sink = app.new(pcap.PcapWriter:new("apps/rumpkernel/selftest.cap.output"))

   -- attach snabb0 to the pcap feed and feed snabb1 to pcap
   app.connect("source", "output", "rumpkernel", "snb0")
   app.connect("rumpkernel", "snb1", "sink", "input")
   app.relink()

   local deadline = lib.timer(1e9)
   repeat app.breathe() until deadline()
   app.report()

   -- Cheat a bit and just grab the TCP packets using tcpdump.
   -- A really simple packet filtering app would be cool, but it's
   -- another show.
   os.execute("tcpdump -r apps/rumpkernel/selftest.cap.output -w " ..
     " apps/rumpkernel/selftest.cap-tcp.output tcp")

   if io.open("apps/rumpkernel/selftest.cap-tcp.output"):read('*a') ~=
      io.open("apps/rumpkernel/selftest.cap.expect"):read('*a') then
      print([[file selftest.cap-tcp.output does not match selftest.cap.expect]])
      print("selftest failed.")
      os.exit(1)
   else
      print("OK.")
   end
end
