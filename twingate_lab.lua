local twig_proto = Proto("TWIG", "Twingate Lab Custom Header")

local f_magic        = ProtoField.uint32("twig.magic",        "Magic",        base.HEX)
local f_version      = ProtoField.uint8 ("twig.version",      "Version",      base.DEC)
local f_flags        = ProtoField.uint8 ("twig.flags",        "Flags",        base.HEX)
local f_payload_len  = ProtoField.uint16("twig.payload_len",  "Payload Len",  base.DEC)
local f_session_id   = ProtoField.uint32("twig.session_id",   "Session ID",   base.DEC)
local f_seq_num      = ProtoField.uint32("twig.seq_num",      "Sequence Num", base.DEC)
local f_timestamp    = ProtoField.uint64("twig.timestamp",    "Timestamp",    base.DEC)
local f_src_tun      = ProtoField.ipv4 ("twig.src_tun",      "Src TUN Addr")
local f_dst_tun      = ProtoField.ipv4 ("twig.dst_tun",      "Dst TUN Addr")
local f_reserved     = ProtoField.bytes("twig.reserved",     "Reserved")

twig_proto.fields = {
    f_magic, f_version, f_flags, f_payload_len,
    f_session_id, f_seq_num, f_timestamp,
    f_src_tun, f_dst_tun, f_reserved
}

function twig_proto.dissector(buffer, pinfo, tree)
    if buffer:len() < 40 then return end
    local subtree = tree:add(twig_proto, buffer(), "Twingate Lab Header")
    subtree:add(f_magic,       buffer(0,4))
    subtree:add(f_version,     buffer(4,1))
    subtree:add(f_flags,       buffer(5,1))
    subtree:add(f_payload_len, buffer(6,2))
    subtree:add(f_session_id,  buffer(8,4))
    subtree:add(f_seq_num,     buffer(12,4))
    subtree:add(f_timestamp,   buffer(16,8))
    subtree:add(f_src_tun,     buffer(24,4))
    subtree:add(f_dst_tun,     buffer(28,4))
    subtree:add(f_reserved,    buffer(32,8))
end

-- Register for UDP port 9999
local udp_table = DissectorTable.get("udp.port")
udp_table:add(9999, twig_proto)
