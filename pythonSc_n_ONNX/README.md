# Edge AI License Plate Detection (ALPR)

This project focuses on Automatic License Plate Recognition (ALPR) optimized for STM32 embedded systems. 

## 🏗️ System Architecture
Due to STM32 hardware limitations, the ALPR pipeline is divided into two stages:
1. **Edge (STM32):** Performs License Plate Detection to locate and crop the plate.
2. **Host (PC/Server):** Receives the `cropped_plate.jpg` from the edge device and performs Character Recognition (OCR) using an EMNIST-based CNN model.

## 📁 Project Structure & Quick Guide

* **`lpr_cnn_embedded_project.py / .ipynb`**: Contains both the plate detection code (for Edge) and the EMNIST-based character recognition code (for Host), though only the detection part is currently deployed to the STM.
* **`data.yaml`**: Configuration file defining dataset paths and class labels for YOLO training.
* **`runs/`**: Contains all training results, evaluation charts (F1, PR curves), and inference logs.
* **`runs/detect/ALPR_Edge/yolov8n_turkish_plates/weights/`**: **Crucial!** This folder contains the exported **ONNX models** (`best.onnx`) used for STM32 embedded deployment.
* **`yolov8n.pt`**: The base pre-trained YOLOv8 nano model used for transfer learning.
* **`cropped_plate.jpg`**: A sample output of the edge detection phase, representing the data sent to the host for letter detection.

## 🚀 Deployment Note
For STM32 deployment, use only the plate detection **ONNX** file located under the `runs/` directory. The EMNIST character recognition part is reserved for the host side.