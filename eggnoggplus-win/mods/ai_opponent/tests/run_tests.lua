-- Standalone test runner: C:\msys64\mingw32\bin\luajit.exe tests/run_tests.lua
-- (cwd must be mods/ai_opponent)
local files = {
  "tests/test_nn.lua",
  "tests/test_actions.lua",
  "tests/test_features.lua",
  "tests/test_policy.lua",
  "tests/test_evo.lua",
  "tests/test_codec.lua",
  "tests/test_heuristic.lua",
}
local failed, ran = 0, 0
_G.check = function(cond, msg)
  ran = ran + 1
  if not cond then
    failed = failed + 1
    print("FAIL: " .. (msg or "?"))
  end
end
for _, f in ipairs(files) do
  local fh = io.open(f, "r")
  if fh then
    fh:close()
    print("== " .. f)
    local ok, err = pcall(dofile, f)
    if not ok then
      failed = failed + 1
      print("ERROR: " .. tostring(err))
    end
  end
end
print(string.format("%d checks, %d failed", ran, failed))
os.exit(failed == 0 and 0 or 1)
