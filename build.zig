const std = @import("std");

pub fn build(b: *std.Build) !void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const gpa = b.allocator;
    const io = b.graph.io;

    const exe = b.addExecutable(.{
        .name = "txted",
        .root_module = b.createModule(.{
            .link_libc = true,
            .target = target,
            .optimize = optimize,
        }),
    });

    // Scan otomatis semua file .c di folder "src"
    var c_files = try std.ArrayList([]const u8).initCapacity(gpa, 128);
    defer c_files.deinit(gpa);

    var dir = std.Io.Dir.cwd().openDir(io, "src", .{
        .iterate = true,
    }) catch unreachable;

    var walker = dir.walk(b.allocator) catch unreachable;
    defer walker.deinit();

    while (walker.next(io) catch unreachable) |entry| {
        // Hanya masukin kalau file C
        if (entry.kind == .file and std.mem.endsWith(u8, entry.path, ".c")) {
            // Join path "src/" + path_file
            const full_path = b.fmt("src/{s}", .{entry.path});
            c_files.append(gpa, full_path) catch unreachable;
        }
    }

    exe.root_module.addIncludePath(b.path("include"));
    exe.root_module.addCSourceFiles(.{
        .files = c_files.items,
        .flags = &.{ "-std=gnu23", "-Wall", "-Wextra", "-O2" },
    });

    // Import module
    exe.root_module.linkSystemLibrary("raylib", .{});
    exe.root_module.linkSystemLibrary("GL", .{});
    exe.root_module.linkSystemLibrary("dl", .{});
    exe.root_module.linkSystemLibrary("rt", .{});
    exe.root_module.linkSystemLibrary("X11", .{});
    exe.root_module.linkSystemLibrary("tree-sitter", .{});

    if (optimize != .Debug) {
        exe.root_module.strip = true;
    }
    b.installArtifact(exe);

    const run_cmd = b.addRunArtifact(exe);
    const run_step = b.step("run", "Run the C application");
    run_step.dependOn(&run_cmd.step);
}
