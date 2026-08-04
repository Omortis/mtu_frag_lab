local wezterm = require 'wezterm'

-- SD-WAN Packet Mangling Lab — WezTerm 6-Pane Layout
--
-- Layout:
--   [ macOS host | VM-A | VM-A ]
--   [ macOS host | VM-B | VM-B ]
--
-- Usage:
--   1. Copy this file to ~/.config/wezterm/mtu_lab_layout.lua
--   2. In ~/.wezterm.lua (or ~/.config/wezterm/wezterm.lua), add:
--        local lab_path = os.getenv("HOME") .. "/.config/wezterm/mtu_lab_layout.lua"
--        local lab = dofile(lab_path)
--        table.insert(config.keys, {
--          key = 'L', mods = 'CTRL|SHIFT',
--          action = wezterm.action_callback(lab.apply),
--        })
--   3. Press Ctrl+Shift+M in WezTerm to spawn the layout.

local function apply_mtu_lab_layout(window, pane)
  -- 1. Split full window vertically.
  --    Left side becomes macOS host. New right pane SSHs into VM-A.
  local vma_top_left = pane:split {
    direction = 'Right',
    size = { Percent = 50 },
    command = { args = { 'ssh', '-l', 'johnnyb', '192.168.64.3' } },
  }

  -- 2. Split original left pane (macOS) horizontally.
  local macos_bottom = pane:split {
    direction = 'Bottom',
    size = { Percent = 50 },
  }

  -- 3. Split right pane (currently VM-A top-left) horizontally.
  --    New bottom pane SSHs into VM-B.
  local vmb_bottom_left = vma_top_left:split {
    direction = 'Bottom',
    size = { Percent = 50 },
    command = { args = { 'ssh', '-l', 'johnnyb', '192.168.64.5' } },
  }

  -- 4. Split top of right side to create 2 VM-A panes.
  local vma_top_right = vma_top_left:split {
    direction = 'Right',
    size = { Percent = 50 },
    command = { args = { 'ssh', '-l', 'johnnyb', '192.168.64.3' } },
  }

  -- 5. Split bottom of right side to create 2 VM-B panes.
  local vmb_bottom_right = vmb_bottom_left:split {
    direction = 'Right',
    size = { Percent = 50 },
    command = { args = { 'ssh', '-l', 'johnnyb', '192.168.64.5' } },
  }

  -- Note: Pane titles are set automatically by the process name.
  -- You can identify VMs by the SSH prompt in each pane.
end

return {
  apply = function(window, pane)
    apply_mtu_lab_layout(window, pane)
  end,
}
