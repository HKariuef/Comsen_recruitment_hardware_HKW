#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
toolchain_dir="${ARM_GNU_TOOLCHAIN_HOME:-${project_dir}/../Comsen_recruitment_hardware/toolchain}"
build_type="${1:-Debug}"

case "${build_type}" in
    Debug|Release|RelWithDebInfo|MinSizeRel) ;;
    *) echo "用法：$0 [Debug|Release|RelWithDebInfo|MinSizeRel]" >&2; exit 2 ;;
esac

[[ -f "${toolchain_dir}/env.sh" ]] || {
    echo "找不到 ${toolchain_dir}/env.sh，请设置 ARM_GNU_TOOLCHAIN_HOME。" >&2
    exit 1
}

source "${toolchain_dir}/env.sh"

for command in arm-none-eabi-gcc cmake ninja; do
    command -v "${command}" >/dev/null 2>&1 || {
        echo "工具链不完整，缺少 ${command}。" >&2
        exit 1
    }
done

cmake -S "${project_dir}" -B "${project_dir}/build/${build_type}" \
      -G Ninja -DCMAKE_BUILD_TYPE="${build_type}"
cmake --build "${project_dir}/build/${build_type}"
