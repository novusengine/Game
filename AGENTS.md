# Novus Game agent guidance

## Windows builds

- Generate or refresh the Visual Studio projects from the repository root with:
  `C:\premake5\premake5.exe vs2022`
- Regenerate after adding or removing source files, changing Premake files, or changing
  metadata definitions under `Source/Meta/Definitions`.
- To perform the Visual Studio-equivalent Game client build, build the generated
  `Game-App.vcxproj` directly. Its `ProjectReference` entries build Game-Lib and all
  required dependencies:

  ```powershell
  cmd /v:on /c 'set NOVUS_BUILD_PATH=%PATH%&&set Path=&&set PATH=!NOVUS_BUILD_PATH!&&"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe" "C:\Projects\Novus\Game\Build\Game-App.vcxproj" /t:Build /p:Configuration="Debug Win64" /p:Platform=x64 /m /nologo'
  ```

- For the Game tests, use the same project configuration and platform against
  `Build\Game-Tests.vcxproj`, then run
  `Build\Bin\Game\Debug\Game-Tests.exe`.
- The generated `.vcxproj` configuration is `Debug Win64|x64`, not
  `Debug|Win64`. The latter names apply at the `.sln` layer and will fail when
  passed directly to a project.
- Do not use a project name such as `/t:Game-App` or `/t:Game-Tests` as an MSBuild
  target. `Build` is the target; the selected `.vcxproj` identifies the project.
- In Codex's Windows sandbox, MSBuild may report `MSB6001` because both `Path` and
  `PATH` are present in the inherited environment. The verified command above
  normalizes them inside a temporary `cmd` process before launching MSBuild.
- Run Premake and MSBuild against the canonical
  `C:\Projects\Novus\Game` checkout path. If compiler output resolves the project
  through a Codex sandbox alias such as
  `C:\Users\CodexSandboxOffline\.codex\.sandbox\cwd\...`, rerun the build outside
  that path-remapping layer. Mixing the alias with Premake's absolute canonical
  include paths makes MSVC include generated headers twice and produces false
  type-redefinition errors.
- Novus builds can take several minutes. Use a generous timeout and do not stop a
  build unless there is concrete evidence that it is hung.
