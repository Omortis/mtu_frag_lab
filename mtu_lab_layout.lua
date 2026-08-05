local wezterm = require 'wezterm'

-- SD-WAN Packet Mangling Lab — WezTerm 6-Pane Layout
--
-- Layout:
--   [ macOS host | VM-A | VM-A ]
--   [ macOS host | VM-B | VM-B ]
--
-- Usage:
--   1. Copy this file to ~/.config/wezterm/mtu_lab_layout.lua
--   2. In ~/.wezterm.lua, add:
--        local lab_path = os.getenv("HOME") .. "/.config/wezterm/mtu_lab_layout.lua"
--        local lab = dofile(lab_path)
--        config.keys = config.keys or {}
--        table.insert(config.keys, {
--          key = 'M', mods = 'CTRL|SHIFT',
--          action = wezterm.action_callback(lab.apply),
--        })
--   3. Press Ctrl+Shift+M in WezTerm to spawn the layout.

local function ssh_into(pane, host)
  -- Send the SSH command as keystrokes after the pane opens.
  -- This is more reliable than SpawnCommand on some WezTerm versions.
  pane:send_text("ssh " .. host .. "\r")
end

local function apply_mtu_lab_layout(window, pane)
  -- 1. Split full window vertically.
  --    Left side becomes macOS host. New right pane is empty (will SSH into VM-A).
  local vma_top_left = pane:split {
    direction = 'Right',
    size = 0.5,
  }

  -- 2. Split original left pane (macOS) horizontally.
  local macos_bottom = pane:split {
    direction = 'Bottom',
    size = 0.5,
  }

  -- 3. Split right pane (currently VM-A top-left) horizontally.
  --    New bottom pane will SSH into VM-B.
  local vmb_bottom_left = vma_top_left:split {
    direction = 'Bottom',
    size = 0.5,
  }

  -- 4. Split top of right side to create 2 VM-A panes.
  local vma_top_right = vma_top_left:split {
    direction = 'Right',
    size = 0.5,
  }

  -- 5. Split bottom of right side to create 2 VM-B panes.
  local vmb_bottom_right = vmb_bottom_left:split {
    direction = 'Right',
    size = 0.5,
  }

  -- SSH into the VM panes after they are created
  ssh_into(vma_top_left, 'johnnyb@192.168.64.3')
  ssh_into(vma_top_right, 'johnnyb@192.168.64.3')
  ssh_into(vmb_bottom_left, 'johnnyb@192.168.64.5')
  ssh_into(vmb_bottom_right, 'johnnyb@192.168.64.5')

  -- Note: Pane titles are set automatically by the process name.
  -- You can identify VMs by the SSH prompt in each pane.
end

return {
  apply = function(window, pane)
    apply_mtu_lab_layout(window, pane)
  end,
}
