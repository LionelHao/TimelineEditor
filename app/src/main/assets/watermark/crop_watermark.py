from PIL import Image
import os

def crop_image_vertical(image_path, output_path, top_percent=15, bottom_percent=15):
    """
    将图片上下各裁切指定百分比
    
    Args:
        image_path: 输入图片路径
        output_path: 输出图片路径
        top_percent: 顶部裁切百分比 (0-100)
        bottom_percent: 底部裁切百分比 (0-100)
    """
    img = Image.open(image_path)
    width, height = img.size
    
    top_crop = int(height * (top_percent / 100))
    bottom_crop = int(height * (bottom_percent / 100))
    
    new_height = height - top_crop - bottom_crop
    
    if new_height <= 0:
        raise ValueError("裁切百分比过大，导致图片高度为负数")
    
    cropped_img = img.crop((0, top_crop, width, height - bottom_crop))
    cropped_img.save(output_path)
    
    print(f"原始尺寸：{width} x {height}")
    print(f"裁切后尺寸：{width} x {new_height}")
    print(f"顶部裁切：{top_crop} 像素 ({top_percent}%)")
    print(f"底部裁切：{bottom_crop} 像素 ({bottom_percent}%)")
    print(f"已保存到：{output_path}")

if __name__ == "__main__":
    script_dir = os.path.dirname(os.path.abspath(__file__))
    input_image = os.path.join(script_dir, "demo.png")
    output_image = os.path.join(script_dir, "demo_cropped.png")
    
    crop_image_vertical(input_image, output_image, top_percent=15, bottom_percent=15)
