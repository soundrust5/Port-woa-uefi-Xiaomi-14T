package com.woa.helper.util

object MountTable {
    data class Entry(val source: String, val target: String, val options: Set<String>) {
        fun hasMode(writable: Boolean) = (if (writable) "rw" else "ro") in options
    }
    fun atTarget(table: String, target: String): Entry? = table.lineSequence().mapNotNull { line ->
        val fields = line.trim().split(Regex("\\s+"))
        if (fields.size < 4) null else Entry(fields[0], fields[1], fields[3].split(',').toSet())
    }.firstOrNull { it.target == target }
    fun matches(table: String, source: String, target: String, writable: Boolean): Boolean =
        atTarget(table, target)?.let { it.source == source && it.hasMode(writable) } ?: false
}
