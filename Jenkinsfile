// Jenkins pipeline for the Prazon/pyrite64 fork.
//
// Builds the editor binary (pyrite64.exe + ./data + ./n64) on a Windows agent
// using MSYS2 UCRT64's GCC + CMake + Ninja, matching the toolchain that
// CLAUDE.md mandates ("On Windows use MSYS2 UCRT64"). Triggered by GitHub
// webhook (githubPush) only — no SCM polling.
//
// To run MSYS2 binaries from Jenkins' bat steps we drive everything through
// `bash.exe -lc` so MSYSTEM=UCRT64 is sourced and PATH includes the UCRT64
// toolchain. Without that, `cmake`/`ninja`/`g++` aren't on PATH for the
// Jenkins service account.
pipeline {
    agent any

    parameters {
        booleanParam(
            name: 'CLEAN_BUILD',
            defaultValue: false,
            description: 'Wipe build/ before configuring (forces full reconfigure + recompile).'
        )
        choice(
            name: 'PRESET',
            choices: ['windows-gcc-release', 'windows-gcc-debug'],
            description: 'CMake preset to configure and build.'
        )
        string(
            name: 'DEPLOY_DIR',
            defaultValue: 'B:\\forks\\pyrite64',
            description: 'Destination working tree to copy pyrite64.exe into after a successful build (so taskbar shortcuts pick up the latest build). Leave empty to skip deploy.'
        )
        booleanParam(
            name: 'RUN_SMOKE',
            defaultValue: true,
            description: 'Run scripts/smoke_test.sh after a successful build (build-tables across all example projects). Disable for hot-fix loops where you want to skip the full table-gen pass.'
        )
    }

    environment {
        // MSYS2 UCRT64 shell: required because the build needs gcc/cmake/ninja
        // from /ucrt64/bin, not the system Git-Bash or PowerShell.
        MSYS2_BASH = 'C:\\msys64\\usr\\bin\\bash.exe'
        MSYSTEM    = 'UCRT64'

        // Final output bundle location. cmake places pyrite64.exe at the
        // workspace root by default; we copy it here alongside the data/ and
        // n64/ trees so the deliverable layout mirrors SPBF's build/Windows
        // convention and so the artifact can be grabbed from one path.
        OUTPUT_DIR = "${WORKSPACE}\\build\\Windows"
    }

    options {
        buildDiscarder(logRotator(numToKeepStr: '15'))
        timestamps()
        timeout(time: 60, unit: 'MINUTES')
        disableConcurrentBuilds()
    }

    triggers {
        // GitHub webhook only — the controller is reachable from github.com,
        // so push notifications drive the pipeline. No SCM polling.
        githubPush()
    }

    stages {
        stage('Setup') {
            steps {
                script { env.FAILED_STAGE = 'Setup' }
                echo "Build #${env.BUILD_NUMBER} starting (preset=${params.PRESET}, clean=${params.CLEAN_BUILD})"
            }
        }

        stage('Submodules') {
            steps {
                script { env.FAILED_STAGE = 'Submodules' }
                // The repo pins SDL, libdragon, tiny3d, etc. via .gitmodules; the
                // build cannot link without them, and the standard checkout step
                // does not recurse by default.
                bat '"%MSYS2_BASH%" -lc "cd \\"$(cygpath \'%WORKSPACE%\')\\" && git submodule update --init --recursive"'
            }
        }

        stage('LFS') {
            steps {
                script { env.FAILED_STAGE = 'LFS' }
                // Pull binary blobs (TTFs, .blend, .mp4, plus the example
                // projects' PNG textures still tracked from upstream). The
                // smoke test will read those PNGs through mksprite, so a
                // pointer-file checkout is not enough.
                bat '"%MSYS2_BASH%" -lc "cd \\"$(cygpath \'%WORKSPACE%\')\\" && git lfs install --local && git lfs pull"'
            }
        }

        stage('Clean') {
            when { expression { return params.CLEAN_BUILD } }
            steps {
                script { env.FAILED_STAGE = 'Clean' }
                bat '"%MSYS2_BASH%" -lc "cd \\"$(cygpath \'%WORKSPACE%\')\\" && rm -rf build pyrite64.exe"'
            }
        }

        stage('Configure') {
            steps {
                script { env.FAILED_STAGE = 'Configure' }
                bat "\"%MSYS2_BASH%\" -lc \"cd \\\"\$(cygpath '%WORKSPACE%')\\\" && cmake --preset ${params.PRESET}\""
            }
        }

        stage('Build') {
            steps {
                script { env.FAILED_STAGE = 'Build' }
                bat "\"%MSYS2_BASH%\" -lc \"cd \\\"\$(cygpath '%WORKSPACE%')\\\" && cmake --build --preset ${params.PRESET}\""
            }
        }

        stage('Verify') {
            steps {
                script { env.FAILED_STAGE = 'Verify' }
                bat '''
                    if exist "%WORKSPACE%\\pyrite64.exe" (
                        echo Build output verified: pyrite64.exe exists.
                    ) else (
                        echo BUILD FAILED - pyrite64.exe not found in %WORKSPACE%
                        exit /b 1
                    )
                '''
            }
        }

        stage('Smoke Test') {
            when { expression { return params.RUN_SMOKE } }
            steps {
                script { env.FAILED_STAGE = 'Smoke Test' }
                // Drives `--cli --cmd build-tables` across the four example
                // projects. Catches regressions in the asset/scene/script
                // table generators without needing the N64 toolchain to
                // produce a ROM. See scripts/smoke_test.sh for the project
                // list and exit semantics.
                bat '"%MSYS2_BASH%" -lc "cd \\"$(cygpath \'%WORKSPACE%\')\\" && bash scripts/smoke_test.sh"'
            }
        }

        stage('Stage Output') {
            steps {
                script { env.FAILED_STAGE = 'Stage Output' }
                // Assemble the runnable bundle in OUTPUT_DIR. CLAUDE.md
                // requires pyrite64.exe to sit next to ./data and ./n64 at
                // runtime, so we mirror that exact layout under build/Windows
                // (matching the SPBF convention) for downstream consumption.
                bat '''
                    if exist "%OUTPUT_DIR%" rmdir /S /Q "%OUTPUT_DIR%"
                    mkdir "%OUTPUT_DIR%"
                    copy /Y "%WORKSPACE%\\pyrite64.exe" "%OUTPUT_DIR%\\pyrite64.exe" >nul
                    xcopy /E /I /Y /Q "%WORKSPACE%\\data" "%OUTPUT_DIR%\\data" >nul
                    xcopy /E /I /Y /Q "%WORKSPACE%\\n64"  "%OUTPUT_DIR%\\n64"  >nul
                    echo Staged output bundle to %OUTPUT_DIR%
                '''
            }
        }

        stage('Archive') {
            steps {
                script { env.FAILED_STAGE = 'Archive' }
                // Archive from the staged output dir so the artifact tree
                // matches the expected runtime layout exactly.
                archiveArtifacts(
                    artifacts: 'build/Windows/**',
                    fingerprint: true,
                    onlyIfSuccessful: true
                )
            }
        }

        stage('Deploy') {
            when { expression { return params.DEPLOY_DIR?.trim() } }
            steps {
                script { env.FAILED_STAGE = 'Deploy' }
                // Drop the freshly built pyrite64.exe into the user's working
                // tree so a taskbar shortcut at <DEPLOY_DIR>\pyrite64.exe
                // always launches the latest CI build. data/ and n64/ already
                // live in that tree as source, so we don't overwrite them
                // here — only the executable.
                bat '''
                    if not exist "%DEPLOY_DIR%" (
                        echo DEPLOY_DIR does not exist: %DEPLOY_DIR%
                        exit /b 1
                    )
                    copy /Y "%WORKSPACE%\\pyrite64.exe" "%DEPLOY_DIR%\\pyrite64.exe" >nul
                    echo Deployed pyrite64.exe to %DEPLOY_DIR%
                '''
            }
        }
    }

    post {
        always {
            echo "Build #${env.BUILD_NUMBER} completed (failed_stage=${env.FAILED_STAGE ?: 'none'})"
        }
    }
}
