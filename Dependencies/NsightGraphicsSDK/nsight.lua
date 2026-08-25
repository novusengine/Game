local dep = Solution.Util.CreateDepTable("NsightGraphicsSDK", {})

Solution.Util.CreateDep(dep.NameLow, dep.Dependencies, function()
    Solution.Util.SetIncludes(dep.Path .. "/include")
end)
