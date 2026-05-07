// Jenkins pipeline for the Prazon/pyrite64 fork.
//
// Builds the editor binary (pyrite64.exe + ./data + ./n64) on a Windows agent
// using MSYS2 UCRT64's GCC + CMake + Ninja, matching the toolchain that
// CLAUDE.md mandates ("On Windows use MSYS2 UCRT64"). The pipeline mirrors
// the structure of D:\CacheGrabGitLab\SPBF\Jenkinsfile (stage-named failure
// tracking, optional Discord notifications) but uses cmake/ninja in place of
// Igor since this is a C++23 codebase, not GameMaker.
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
            name: 'DISCORD_WEBHOOK',
            defaultValue: '',
            description: 'Optional Discord webhook URL for build notifications. Leave empty to skip.'
        )
    }

    environment {
        // MSYS2 UCRT64 shell: required because the build needs gcc/cmake/ninja
        // from /ucrt64/bin, not the system Git-Bash or PowerShell.
        MSYS2_BASH = 'C:\\msys64\\usr\\bin\\bash.exe'
        MSYSTEM    = 'UCRT64'
    }

    options {
        buildDiscarder(logRotator(numToKeepStr: '15'))
        timestamps()
        timeout(time: 60, unit: 'MINUTES')
        disableConcurrentBuilds()
    }

    triggers {
        // Poll GitHub for changes; switch to a webhook later if/when the
        // Jenkins controller is reachable from github.com.
        pollSCM('H/15 * * * *')
    }

    stages {
        stage('Setup') {
            steps {
                script { env.FAILED_STAGE = 'Setup' }
                script {
                    if (params.DISCORD_WEBHOOK?.trim()) {
                        discordSend(
                            webhookURL: params.DISCORD_WEBHOOK,
                            title: "pyrite64 Build #${env.BUILD_NUMBER} - STARTED",
                            description: "Preset: ${params.PRESET}\nClean: ${params.CLEAN_BUILD}",
                            result: 'UNSTABLE',
                            showChangeset: true
                        )
                    }
                }
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

        stage('Archive') {
            steps {
                script { env.FAILED_STAGE = 'Archive' }
                // Archive the runnable bundle: the .exe alongside the data/ and
                // n64/ trees it expects at runtime (per CLAUDE.md). Skip build/
                // and vendored/ to keep the artifact lean.
                archiveArtifacts(
                    artifacts: 'pyrite64.exe, data/**, n64/**',
                    fingerprint: true,
                    onlyIfSuccessful: true
                )
            }
        }
    }

    post {
        success {
            script {
                if (params.DISCORD_WEBHOOK?.trim()) {
                    discordSend(
                        webhookURL: params.DISCORD_WEBHOOK,
                        title: "pyrite64 Build #${env.BUILD_NUMBER} - SUCCESS",
                        description: "Preset: ${params.PRESET}\nCommit: ${env.GIT_COMMIT?.take(8) ?: 'unknown'}",
                        result: 'SUCCESS',
                        showChangeset: true
                    )
                }
            }
        }
        failure {
            script {
                if (params.DISCORD_WEBHOOK?.trim()) {
                    discordSend(
                        webhookURL: params.DISCORD_WEBHOOK,
                        title: "pyrite64 Build #${env.BUILD_NUMBER} - FAILED",
                        description: "Failed at stage: **${env.FAILED_STAGE ?: 'unknown'}**\nPreset: ${params.PRESET}\nCheck console output for details.",
                        result: 'FAILURE',
                        showChangeset: true
                    )
                }
            }
        }
        always {
            echo "Build #${env.BUILD_NUMBER} completed (failed_stage=${env.FAILED_STAGE ?: 'none'})"
        }
    }
}
