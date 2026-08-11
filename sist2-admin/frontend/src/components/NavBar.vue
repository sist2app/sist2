<template>
    <nav class="navbar navbar-expand bg-white">
        <div class="container">
            <a class="navbar-brand" href="#/">
                <Sist2Icon/>
            </a>
            <a class="btn btn-link ms-auto" href="#/tasks">Tasks</a>
        </div>
    </nav>
</template>

<script setup>
import Sist2Icon from "./icons/Sist2Icon.vue";
</script>

<style scoped>
.navbar {
    box-shadow: 0 0.125rem 0.25rem rgb(0 0 0 / 8%);
    border-radius: 0;
}

.navbar-brand {
    color: #222;
    font-size: 1.75rem;
    padding: 0;
}

.navbar-brand:hover {
    color: #000;
}

.btn-link {
    color: #222;
    text-decoration: none;
}
</style>
