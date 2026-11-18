@echo off
rem Weekly ASan corpus sweep (roadmap mcx7 t05): the MSVC tree (build_agent.cmd msvc all, no tests), the ASan tree with every
rem module (build_asan.cmd, ALAMO_SANITIZE=address: it also runs the whole ctest under ASan), then the corpus sweep with
rem --asan: the headless runs again under the ASan build's alamo_headless; a sanitizer report keeps the mod below G2.
rem   tools\modcorpus\asan_sweep.cmd [more run_corpus options, e.g. --only 1125571106]
rem Smokes are left out (the ASan tree's alamo_game is not used). Results: logs\modcorpus\<date>-asan\ (matrix.json, one
rem JSON per setup), the log logs\modcorpus\run_corpus.txt. Exit: the sweep's (1 = a grade dropped since the last ASan
rem sweep), 4 = a build failed. tools\modcorpus\install_asan_sweep_task.cmd registers it weekly; nothing registers it for you.
setlocal
cd /d "%~dp0..\.."
call "%CD%\build_agent.cmd" msvc all none
if errorlevel 1 (echo asan_sweep: MSVC build failed& exit /b 4)
set "ALAMO_SANITIZE=address"
call "%CD%\build_asan.cmd" "core;vfs;data;jobs;galactic;objects;anim;commands;tactical;lua;ai;assets;rhi;shaderxlat;materials;terrain;render;ui;audio;video;apps"
if not exist "%CD%\build-asan\bin\alamo_headless.exe" (echo asan_sweep: ASan build has no alamo_headless.exe& exit /b 4)
for /f %%D in ('python -c "import datetime;print(datetime.date.today().isoformat())"') do set "TODAY=%%D"
call "%CD%\tools\modcorpus\run_corpus.cmd" --asan --stages check,runs --date %TODAY%-asan %*
exit /b %ERRORLEVEL%
