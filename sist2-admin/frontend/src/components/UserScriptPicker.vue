<template>
    <div class="card mb-3">
        <div class="card-header">User scripts</div>
        <div class="card-body">
            <div class="row">
                <div class="col-md-6">
                    <label class="form-label">Selected</label>
                    <ul class="list-group">
                        <li class="list-group-item d-flex justify-content-between align-items-center"
                            v-for="(name, index) in selected" :key="name">
                            {{ name }}
                            <span>
                                <button class="btn btn-sm btn-outline-secondary" :disabled="index === 0"
                                        @click="move(index, -1)">↑</button>
                                <button class="btn btn-sm btn-outline-secondary ms-1"
                                        :disabled="index === selected.length - 1" @click="move(index, 1)">↓</button>
                                <button class="btn btn-sm btn-outline-danger ms-1" @click="remove(index)">×</button>
                            </span>
                        </li>
                    </ul>
                </div>
                <div class="col-md-6">
                    <label class="form-label">Available</label>
                    <ul class="list-group">
                        <li class="list-group-item list-group-item-action" style="cursor: pointer"
                            v-for="name in available" :key="name" @click="add(name)">
                            {{ name }}
                        </li>
                    </ul>
                </div>
            </div>
        </div>
    </div>
</template>

<script setup>
import { computed, onMounted, ref } from "vue";

import { api } from "../api.js";

const props = defineProps({
    selected: { type: Array, required: true }
});

const allScripts = ref([]);

const available = computed(() => {
    return allScripts.value
        .map((script) => script.name)
        .filter((name) => !props.selected.includes(name));
});

function add(name) {
    props.selected.push(name);
}

function remove(index) {
    props.selected.splice(index, 1);
}

function move(index, delta) {
    const target = index + delta;
    const [item] = props.selected.splice(index, 1);
    props.selected.splice(target, 0, item);
}

onMounted(async () => {
    allScripts.value = await api.get("/api/user_script");
});
</script>
